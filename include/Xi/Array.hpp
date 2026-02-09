#ifndef XI_ARRAY_HPP
#define XI_ARRAY_HPP 1

#include "Primitives.hpp"
#include <cstdio> // For printf safety check

// Small allocation block size.
#if defined(AVR) || defined(ARDUINO)
#define XI_ARRAY_MIN_CAP 4
#else
#define XI_ARRAY_MIN_CAP 16
#endif

namespace Xi
{
    // -------------------------------------------------------------------------
    // ArrayFragment
    // Ref-counted block of memory.
    // -------------------------------------------------------------------------
    template <typename T>
    struct ArrayFragment
    {
        T *allocation;
        usz capacity;
        T *data;
        usz length;
        usz *useCount;

        static ArrayFragment *create(usz needed_cap, usz start_offset = 0)
        {
            ArrayFragment *f = new ArrayFragment();

            usz cap = needed_cap + start_offset;
            if (cap < XI_ARRAY_MIN_CAP)
                cap = XI_ARRAY_MIN_CAP;

            f->capacity = cap;
            // Use global new to avoid constructor overhead for T
            f->allocation = (T *)::operator new(sizeof(T) * cap);
            f->useCount = new usz(1);

            // Safety clamp
            if (start_offset > cap)
                start_offset = cap;

            f->data = f->allocation + start_offset;
            f->length = 0;
            return f;
        }

        ArrayFragment *clone()
        {
            ArrayFragment *cp = create(length, 0);
            for (usz i = 0; i < length; ++i)
            {
                new (&cp->data[i]) T(data[i]);
            }
            cp->length = length;
            return cp;
        }

        inline void retain()
        {
            if (useCount)
                (*useCount)++;
        }

        inline bool release()
        {
            if (!useCount)
                return true; // Already null

            // CHECK FOR GARBAGE POINTER
            // If useCount is 0xDEADBEEF, we are reading freed memory.
            if ((usz)useCount == 0xDEADBEEFDEADBEEF)
            {
                fprintf(stderr, "FATAL: Accessing freed fragment %p\n", this);
                __builtin_trap();
            }

            if (*useCount == 0)
            {
                fprintf(stderr, "FATAL: Double free on fragment %p\n", this);
                return true;
            }

            if (--(*useCount) == 0)
            {
                // ... destructor loop ...
                if (allocation)
                    ::operator delete(allocation);

                // POISON THE MEMORY
                *useCount = 0xDEADBEEF;
                delete useCount;

                useCount = nullptr;
                allocation = nullptr;
                delete this;
                return true;
            }
            return false;
        }

        inline usz tail_room() const
        {
            const T *end_alloc = allocation + capacity;
            const T *end_data = data + length;
            if (end_data >= end_alloc)
                return 0;
            return (usz)(end_alloc - end_data);
        }

        inline usz head_room() const
        {
            if (data <= allocation)
                return 0;
            return (usz)(data - allocation);
        }
    };

    // -------------------------------------------------------------------------
    // ArrayUse
    // Handle to a fragment. Rule of 5 implemented.
    // -------------------------------------------------------------------------
    template <typename T>
    struct ArrayUse
    {
        ArrayFragment<T> *fragment;
        usz at;
        bool own;
        bool reverse;

        // Default initialization to nulls is CRITICAL for raw memory safety
        ArrayUse() : fragment(nullptr), at(0), own(true), reverse(false) {}

        ~ArrayUse()
        {
            if (own && fragment)
                fragment->release();
            fragment = nullptr;
        }

        ArrayUse(const ArrayUse &o) : fragment(o.fragment), at(o.at), own(o.own), reverse(o.reverse)
        {
            if (own && fragment)
                fragment->retain();
        }

        ArrayUse(ArrayUse &&o) noexcept : fragment(o.fragment), at(o.at), own(o.own), reverse(o.reverse)
        {
            o.fragment = nullptr;
            o.own = false;
            o.at = 0;
        }

        ArrayUse &operator=(const ArrayUse &o)
        {
            if (this == &o)
                return *this;
            if (o.own && o.fragment)
                o.fragment->retain();
            if (own && fragment)
                fragment->release();

            fragment = o.fragment;
            at = o.at;
            own = o.own;
            reverse = o.reverse;
            return *this;
        }

        ArrayUse &operator=(ArrayUse &&o) noexcept
        {
            if (this == &o)
                return *this;
            if (own && fragment)
                fragment->release();

            fragment = o.fragment;
            at = o.at;
            own = o.own;
            reverse = o.reverse;

            o.fragment = nullptr;
            o.own = false;
            o.at = 0;
            return *this;
        }
    };

    // -------------------------------------------------------------------------
    // Array
    // -------------------------------------------------------------------------
    template <typename T>
    class Array
    {
    public:
        // UseList: Uses new[]/delete[] for safety.
        struct UseList
        {
            ArrayUse<T> *list;
            usz count;
            usz cap;

            UseList() : list(nullptr), count(0), cap(0) {}

            ~UseList()
            {
                // Manually destroy only the active objects
                for (usz i = 0; i < count; ++i)
                {
                    list[i].~ArrayUse();
                }
                // Free raw memory
                if (list)
                    ::operator delete(list);
            }

            void clear()
            {
                if (list)
                {
                    for (usz i = 0; i < count; ++i)
                        list[i].~ArrayUse();
                }
                count = 0;
            }

            void reserve(usz n)
            {
                if (n <= cap)
                    return;
                usz newCap = (n < 4) ? 4 : n;

                ArrayUse<T> *newList = (ArrayUse<T> *)::operator new(sizeof(ArrayUse<T>) * newCap);

                // FIX: Zero-init to ensure no garbage pointers exist
                memset(newList, 0, sizeof(ArrayUse<T>) * newCap);

                for (usz i = 0; i < count; ++i)
                {
                    new (&newList[i]) ArrayUse<T>(Xi::Move(list[i]));
                    list[i].~ArrayUse();
                }

                if (list)
                    ::operator delete(list);
                list = newList;
                cap = newCap;
            }

            void push(ArrayUse<T> u)
            {
                if (count == cap)
                    reserve(cap == 0 ? 4 : cap * 2);

                // CRITICAL FIX: Use placement new. DO NOT USE ASSIGNMENT.
                // Assignment reads uninitialized 'own/fragment' flags and crashes.
                new (&list[count++]) ArrayUse<T>(Xi::Move(u));
            }

            void insert(usz idx, ArrayUse<T> u)
            {
                if (count == cap)
                    reserve(cap == 0 ? 4 : cap * 2);

                if (idx < count)
                {
                    // 1. Move construct last element into raw slot
                    new (&list[count]) ArrayUse<T>(Xi::Move(list[count - 1]));

                    // 2. Move assign the overlapping middle section (these are valid objects)
                    for (usz i = count - 1; i > idx; --i)
                    {
                        list[i] = Xi::Move(list[i - 1]);
                    }

                    // 3. Assign new value to the hole (valid object)
                    list[idx] = Xi::Move(u);
                }
                else
                {
                    // Appending to end: Use placement new
                    new (&list[idx]) ArrayUse<T>(Xi::Move(u));
                }
                count++;
            }

            void remove(usz idx)
            {
                if (idx >= count)
                    return;

                // Move everything down
                for (usz i = idx; i < count - 1; ++i)
                {
                    list[i] = Xi::Move(list[i + 1]);
                }

                // Explicitly destroy the last element which is now valid but unused
                list[count - 1].~ArrayUse();
                count--;
            }

            inline ArrayUse<T> &operator[](usz i) { return list[i]; }
            inline const ArrayUse<T> &operator[](usz i) const { return list[i]; }
        };

        UseList uses;
        usz length;

        void recompute()
        {
            usz g = 0;
            for (usz i = 0; i < uses.count; ++i)
            {
                uses[i].at = g;
                if (uses[i].fragment)
                    g += uses[i].fragment->length;
            }
            length = g;
        }

    private:
        void ensure_unique(usz useIdx)
        {
            if (useIdx >= uses.count)
                return;
            ArrayUse<T> &u = uses[useIdx];
            if (u.fragment && *u.fragment->useCount > 1)
            {
                ArrayFragment<T> *c = u.fragment->clone();
                u.fragment->release();
                u.fragment = c;
            }
        }
        usz find_use_index(usz global, usz &offset) const
        {
            if (uses.count == 0)
                return (usz)-1;

            for (usz i = 0; i < uses.count; ++i)
            {
                ArrayFragment<T> *f = uses[i].fragment;
                if (!f)
                    continue;

                usz start = uses[i].at;
                usz end = start + f->length;

                if (global >= start && global < end)
                {
                    offset = global - start;
                    return i;
                }
            }
            return (usz)-1;
        }

    public:
        Array() : length(0) {}

        // Virtual Destructor ensures ABI compatibility for polymorphic types
        virtual ~Array() {}

        Array(const Array &o) : length(o.length)
        {
            uses.reserve(o.uses.count);
            for (usz i = 0; i < o.uses.count; ++i)
                uses.push(o.uses[i]);
        }

        Array(Array &&o) noexcept : length(o.length)
        {
            uses.list = o.uses.list;
            uses.count = o.uses.count;
            uses.cap = o.uses.cap;

            o.uses.list = nullptr;
            o.uses.count = 0;
            o.uses.cap = 0;
            o.length = 0;
        }

        Array &operator=(const Array &o)
        {
            if (this == &o)
                return *this;
            uses.clear();
            uses.reserve(o.uses.count);
            for (usz i = 0; i < o.uses.count; ++i)
            {
                uses.push(o.uses[i]);
            }
            length = o.length;
            return *this;
        }

        // Explicit Move Assignment
        Array &operator=(Array &&o) noexcept
        {
            if (this == &o)
                return *this;

            // Clear current resources
            uses.clear();
            if (uses.list)
                ::operator delete(uses.list);

            // Steal resources
            uses.list = o.uses.list;
            uses.count = o.uses.count;
            uses.cap = o.uses.cap;
            length = o.length;

            // Reset source
            o.uses.list = nullptr;
            o.uses.count = 0;
            o.uses.cap = 0;
            o.length = 0;

            return *this;
        }

        inline bool has(usz index) const { return index < length; }

        Array<T> begin() const { return *this; }
        Array<T> end() const { return Array<T>(); }
        bool operator!=(const Array<T> &other) const { return length > 0; }
        T &operator*() { return (*this)[0]; }
        Array<T> &operator++()
        {
            shift();
            return *this;
        }

        void alloc(usz fwd, usz back = 0)
        {
            if (fwd == 0 && back == 0)
                return;
            ArrayFragment<T> *f = ArrayFragment<T>::create(fwd, back);
            for (usz i = 0; i < fwd; ++i)
                new (&f->data[i]) T();
            f->length = fwd;
            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.push(Xi::Move(u));
            recompute();
        }

        T *data()
        {
            if (length == 0)
                return nullptr;
            if (uses.count == 1 && !uses[0].reverse)
                return uses[0].fragment->data;
            ArrayFragment<T> *flat = ArrayFragment<T>::create(length, 0);
            usz w = 0;
            for (usz i = 0; i < uses.count; ++i)
            {
                ArrayUse<T> &u = uses[i];
                T *src = u.fragment->data;
                usz cnt = u.fragment->length;
                if (w + cnt > flat->capacity)
                    cnt = flat->capacity - w;
                if (!u.reverse)
                {
                    for (usz k = 0; k < cnt; ++k)
                        new (&flat->data[w++]) T(src[k]);
                }
                else
                {
                    for (usz k = cnt; k > 0; --k)
                        new (&flat->data[w++]) T(src[k - 1]);
                }
            }
            flat->length = w;
            uses.clear();
            ArrayUse<T> u;
            u.fragment = flat;
            u.own = true;
            uses.push(Xi::Move(u));
            recompute();
            return flat->data;
        }
        const T *data() const { return const_cast<Array *>(this)->data(); }

        T shift()
        {
            if (length == 0)
                return T();
            ensure_unique(0);
            ArrayUse<T> &u = uses[0];
            ArrayFragment<T> *f = u.fragment;
            T ret;
            if (!u.reverse)
            {
                ret = Xi::Move(f->data[0]);
                f->data[0].~T();
                f->data++;
                f->length--;
            }
            else
            {
                ret = Xi::Move(f->data[f->length - 1]);
                f->data[f->length - 1].~T();
                f->length--;
            }
            length--;
            if (f->length == 0)
                uses.remove(0);
            else
                for (usz i = 1; i < uses.count; ++i)
                    uses[i].at--;
            return ret;
        }

        T pop()
        {
            if (length == 0)
                return T();
            usz idx = uses.count - 1;
            ensure_unique(idx);
            ArrayUse<T> &u = uses[idx];
            ArrayFragment<T> *f = u.fragment;
            T ret;
            if (!u.reverse)
            {
                ret = Xi::Move(f->data[f->length - 1]);
                f->data[f->length - 1].~T();
                f->length--;
            }
            else
            {
                ret = Xi::Move(f->data[0]);
                f->data[0].~T();
                f->data++;
                f->length--;
            }
            length--;
            if (f->length == 0)
                uses.remove(idx);
            return ret;
        }

        void grow_at_back(usz new_capacity)
        {
            ArrayFragment<T> *f = ArrayFragment<T>::create(new_capacity, 0);
            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            u.at = length;
            uses.push(Xi::Move(u));
        }

        void grow_at_front(usz new_capacity)
        {
            ArrayFragment<T> *f = ArrayFragment<T>::create(0, new_capacity);
            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.insert(0, Xi::Move(u));
        }

        void push(const T &val)
        {
            if (uses.count == 0)
            {
                this->grow_at_back(XI_ARRAY_MIN_CAP);
            }

            if (uses[uses.count - 1].fragment->tail_room() == 0)
            {
                this->grow_at_back(uses[uses.count - 1].fragment->capacity * 2);
            }

            ArrayUse<T> &u = uses[uses.count - 1];
            ArrayFragment<T> *f = u.fragment;

            new (&f->data[f->length]) T(val);
            f->length++;
            this->recompute();
        }

        long long find(const T &needle, usz start = 0) const
        {
            usz currentGlobal = 0;
            for (usz i = 0; i < uses.count; ++i)
            {
                const ArrayUse<T> &u = uses[i];
                usz len = u.fragment->length;
                if (currentGlobal + len <= start)
                {
                    currentGlobal += len;
                    continue;
                }
                usz k = (currentGlobal < start) ? (start - currentGlobal) : 0;
                T *raw = u.fragment->data;
                for (; k < len; ++k)
                {
                    usz pIdx = u.reverse ? (len - 1 - k) : k;
                    if (raw[pIdx] == needle)
                        return (long long)(currentGlobal + k);
                }
                currentGlobal += len;
            }
            return -1;
        }

        Array<T> reverse() const
        {
            Array<T> res;
            res.uses.reserve(uses.count);
            for (usz i = uses.count; i > 0; --i)
            {
                ArrayUse<T> u = uses[i - 1];
                u.reverse = !u.reverse;
                res.uses.push(Xi::Move(u));
            }
            res.recompute();
            return res;
        }

        void break_at(usz index)
        {
            if (index == 0 || index >= length)
                return;
            usz off;
            usz idx = find_use_index(index, off);
            if (idx == (usz)-1 || off == 0)
                return;
            ArrayFragment<T> *old = uses[idx].fragment;
            bool isRev = uses[idx].reverse;
            usz oldAt = uses[idx].at;
            usz lenA = off;
            usz lenB = old->length - off;
            ArrayFragment<T> *fA = ArrayFragment<T>::create(lenA, 0);
            ArrayFragment<T> *fB = ArrayFragment<T>::create(lenB, 0);
            T *src = old->data;
            if (!isRev)
            {
                for (usz i = 0; i < lenA; ++i)
                    new (&fA->data[i]) T(src[i]);
                for (usz i = 0; i < lenB; ++i)
                    new (&fB->data[i]) T(src[lenA + i]);
            }
            else
            {
                for (usz i = 0; i < lenA; ++i)
                    new (&fA->data[i]) T(src[old->length - 1 - i]);
                for (usz i = 0; i < lenB; ++i)
                    new (&fB->data[i]) T(src[lenB - 1 - i]);
            }
            fA->length = lenA;
            fB->length = lenB;
            uses[idx].fragment->release();
            uses[idx].fragment = fA;
            uses[idx].reverse = false;
            uses[idx].at = oldAt;
            ArrayUse<T> uB;
            uB.fragment = fB;
            uB.own = true;
            uses.insert(idx + 1, Xi::Move(uB));
            recompute();
        }

        Array<T> begin(usz from, usz to)
        {
            usz end = (to == 0) ? length : to;
            if (from >= end)
                return Array<T>();
            break_at(from);
            break_at(end);
            Array<T> sub;
            usz g = 0;
            for (usz i = 0; i < uses.count; ++i)
            {
                usz len = uses[i].fragment->length;
                if (g >= from && (g + len) <= end)
                    sub.uses.push(uses[i]);
                g += len;
            }
            sub.recompute();
            return sub;
        }

        Array<T> splice(usz from, usz to = 0)
        {
            Array<T> sub = begin(from, to);
            usz end = (to == 0) ? length : to;
            for (usz i = uses.count; i > 0; --i)
            {
                usz idx = i - 1;
                usz s = uses[idx].at;
                usz l = uses[idx].fragment->length;
                if (s >= from && (s + l) <= end)
                    uses.remove(idx);
            }
            recompute();
            return sub;
        }

        void concat(const Array<T> &other)
        {
            for (usz i = 0; i < other.uses.count; ++i)
                uses.push(other.uses[i]);
            recompute();
        }

        T &operator[](usz i)
        {
            usz off;
            usz idx = find_use_index(i, off);
            if (idx == (usz)-1)
            {
                static T dummy;
                return dummy;
            }
            ensure_unique(idx);
            ArrayUse<T> &u = uses[idx];
            if (u.reverse)
                return u.fragment->data[u.fragment->length - 1 - off];
            return u.fragment->data[off];
        }

        const T &operator[](usz i) const
        {
            usz off;
            usz idx = find_use_index(i, off);
            if (idx == (usz)-1)
            {
                static T dummy;
                return dummy;
            }
            const ArrayUse<T> &u = uses[idx];
            if (u.reverse)
                return u.fragment->data[u.fragment->length - 1 - off];
            return u.fragment->data[off];
        }

        void pushEach(const T *ptr, usz cnt)
        {
            if (!ptr || cnt == 0)
                return;
            ArrayFragment<T> *f = ArrayFragment<T>::create(cnt, 0);
            for (usz i = 0; i < cnt; ++i)
                new (&f->data[i]) T(ptr[i]);
            f->length = cnt;

            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.push(Xi::Move(u));
            length += cnt;
            recompute();
        }

        void set(const Array<T> &o, usz idx = 0)
        {
            for (usz i = 0; i < o.length; ++i)
            {
                if (idx + i < length)
                    (*this)[idx + i] = o[i];
                else
                    push(o[i]);
            }
        }

        void unshift(const T &val)
        {
            if (uses.count == 0)
                this->grow_at_front(XI_ARRAY_MIN_CAP);

            if (uses[0].fragment->head_room() == 0)
            {
                this->grow_at_front(uses[0].fragment->capacity * 2);
            }

            ArrayUse<T> &u = uses[0];
            ArrayFragment<T> *f = u.fragment;

            if (f->data > f->allocation)
            {
                f->data--;
                new (f->data) T(val);
                f->length++;
            }
            this->recompute();
        }

        void set(const T *src_ptr, usz input_cnt, usz start_idx = 0)
        {
            for (usz i = 0; i < input_cnt; ++i)
            {
                if (start_idx + i < this->length)
                    (*this)[start_idx + i] = src_ptr[i];
                else
                    this->push(src_ptr[i]);
            }
        }

        void remove(usz index)
        {
            if (index >= length)
                return;
            splice(index, index + 1);
        }

        void clear()
        {
            uses.clear();
            length = 0;
        }

        static void check_abi()
        {
            printf("[Xi::Array ABI] sizeof(ArrayUse<u8>): %zu\n", sizeof(ArrayUse<T>));
            printf("[Xi::Array ABI] sizeof(Array<u8>): %zu\n", sizeof(Array<T>));
            printf("[Xi::Array ABI] offsetof(uses): %zu\n", (usz) & ((Array<T> *)0)->uses);
        }
    };
}

#endif // XI_ARRAY_HPP