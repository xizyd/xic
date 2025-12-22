// src/Xi/Array.hpp

#ifndef XI_ARRAY_HPP
#define XI_ARRAY_HPP 1

#include "Utils.hpp"

// Small allocation block size.
// On Arduino (low RAM), keep small (8). On Servers, can be larger (e.g. 64).
#if defined(__AVR__) || defined(ARDUINO)
#define XI_ARRAY_MIN_CAP 4
#else
#define XI_ARRAY_MIN_CAP 16
#endif

namespace Xi
{

    // ArrayFragment
    // The physical container of data. shared between Arrays via Ref-Counting.
    template <typename T>
    struct ArrayFragment
    {
        T *allocation; // Start of the malloc'd block
        usz capacity;  // Total capacity in items
        T *data;       // Pointer to active start
        usz length;    // Active count
        usz *useCount; // Ref counter

        // Create a fragment with specific headroom (space before)
        static ArrayFragment *create(usz needed, usz headroom = 0)
        {
            ArrayFragment *f = new ArrayFragment();
            usz cap = needed + headroom;
            if (cap < XI_ARRAY_MIN_CAP)
                cap = XI_ARRAY_MIN_CAP;

            f->capacity = cap;
            // Allocate raw memory for items (constructors not called yet)
            f->allocation = (T *)::operator new(sizeof(T) * cap);
            f->useCount = new usz(1);

            // Position data pointer
            f->data = f->allocation + headroom;
            f->length = 0;
            return f;
        }

        // Deep Copy (Clone)
        ArrayFragment *clone()
        {
            ArrayFragment *cp = create(length, 0); // Tight pack
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

        // Returns true if memory was freed
        inline bool release()
        {
            if (!useCount)
                return true;
            (*useCount)--;
            if (*useCount == 0)
            {
                // Destruct items
                for (usz i = 0; i < length; ++i)
                    data[i].~T();
                // Free raw memory
                ::operator delete(allocation);
                delete useCount;
                delete this;
                return true;
            }
            return false;
        }

        // Capacity checks
        inline usz head_cap() const { return data - allocation; }
        inline usz tail_cap() const { return (allocation + capacity) - (data + length); }
    };

    // ArrayUse
    // A lightweight handle pointing to a Fragment (or part of it).
    template <typename T>
    struct ArrayUse
    {
        ArrayFragment<T> *fragment;
        usz at;       // Global index logic
        bool own;     // Does this handle own a reference?
        bool reverse; // Read backwards?

        ArrayUse() : fragment(null), at(0), own(true), reverse(false) {}

        // Copy Ctor: Inc Ref
        ArrayUse(const ArrayUse &o) : fragment(o.fragment), at(o.at), own(o.own), reverse(o.reverse)
        {
            if (own && fragment)
                fragment->retain();
        }

        // Move Ctor: Steal Ref
        ArrayUse(ArrayUse &&o) : fragment(o.fragment), at(o.at), own(o.own), reverse(o.reverse)
        {
            o.fragment = null;
            o.own = false;
        }

        // Assignment
        ArrayUse &operator=(ArrayUse o)
        {
            Xi::Swap(fragment, o.fragment);
            Xi::Swap(at, o.at);
            Xi::Swap(own, o.own);
            Xi::Swap(reverse, o.reverse);
            return *this;
        }

        ~ArrayUse()
        {
            if (own && fragment)
                fragment->release();
        }
    };

    // Array
    // The main container. Acts as a View, a Container, and an Iterator.
    template <typename T>
    class Array
    {
    private:
        // Internal Optimized List (replaces std::vector)
        // Uses raw memory to strictly control construction/destruction
        struct UseList
        {
            ArrayUse<T> *list;
            usz count;
            usz cap;

            UseList() : list(null), count(0), cap(0) {}

            ~UseList()
            {
                clear();
                if (list)
                    ::operator delete(list);
            }

            void clear()
            {
                if (!list)
                    return;
                for (usz i = 0; i < count; ++i)
                    list[i].~ArrayUse();
                count = 0;
            }

            void reserve(usz n)
            {
                if (n <= cap)
                    return;
                usz newCap = (n < 4) ? 4 : n;

                // Raw Alloc
                ArrayUse<T> *newList = (ArrayUse<T> *)::operator new(sizeof(ArrayUse<T>) * newCap);

                // Move Construct
                for (usz i = 0; i < count; ++i)
                {
                    new (&newList[i]) ArrayUse<T>(Xi::Move(list[i]));
                    list[i].~ArrayUse(); // Destruct old shell
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
                new (&list[count++]) ArrayUse<T>(Xi::Move(u));
            }

            void insert(usz idx, ArrayUse<T> u)
            {
                if (count == cap)
                    reserve(cap == 0 ? 4 : cap * 2);
                if (idx < count)
                {
                    // Shift Right
                    new (&list[count]) ArrayUse<T>(Xi::Move(list[count - 1]));
                    for (usz i = count - 1; i > idx; --i)
                    {
                        list[i] = Xi::Move(list[i - 1]);
                    }
                    list[idx].~ArrayUse(); // Clear slot
                    new (&list[idx]) ArrayUse<T>(Xi::Move(u));
                }
                else
                {
                    new (&list[idx]) ArrayUse<T>(Xi::Move(u));
                }
                count++;
            }

            void remove(usz idx)
            {
                if (idx >= count)
                    return;
                for (usz i = idx; i < count - 1; ++i)
                {
                    list[i] = Xi::Move(list[i + 1]);
                }
                list[count - 1].~ArrayUse();
                count--;
            }

            inline ArrayUse<T> &operator[](usz i) { return list[i]; }
            inline const ArrayUse<T> &operator[](usz i) const { return list[i]; }
        };

        UseList uses;

        // -------------------------------------------------------------------------
        // Internal Helpers
        // -------------------------------------------------------------------------

        // Re-calculates total length and the 'at' cache for every fragment
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

        // Copy-On-Write: Ensures the fragment at useIdx is unique (refCount == 1)
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

        // Locates which fragment holds the global index.
        // Sets 'offset' to the local index within that fragment.
        usz find_use_index(usz global, usz &offset) const
        {
            // Optimization: Check last fragment first (common for push/access patterns)
            if (uses.count > 0)
            {
                const auto &last = uses[uses.count - 1];
                if (global >= last.at && global < last.at + last.fragment->length)
                {
                    offset = global - last.at;
                    return uses.count - 1;
                }
            }
            // Linear scan (Fragmentation is usually low)
            for (usz i = 0; i < uses.count; ++i)
            {
                usz flen = uses[i].fragment->length;
                if (global < uses[i].at + flen)
                {
                    offset = global - uses[i].at;
                    return i;
                }
            }
            return (usz)-1;
        }

    public:
        // Length of the array.
        usz length;

        // Constructors & Assignment
        Array() : length(0) {}

        Array(const Array &o) : length(o.length)
        {
            uses.reserve(o.uses.count);
            for (usz i = 0; i < o.uses.count; ++i)
                uses.push(o.uses[i]);
        }

        Array(Array &&o) : length(o.length)
        {
            // Steal internal UseList
            uses.list = o.uses.list;
            uses.count = o.uses.count;
            uses.cap = o.uses.cap;

            o.uses.list = null;
            o.uses.count = 0;
            o.uses.cap = 0;
            o.length = 0;
        }

        Array &operator=(Array o)
        {
            Xi::Swap(uses.list, o.uses.list);
            Xi::Swap(uses.count, o.uses.count);
            Xi::Swap(uses.cap, o.uses.cap);
            Xi::Swap(length, o.length);
            return *this;
        }

        ~Array()
        {
            // uses dtor handles cleanup
        }

        // Does that index point to a real element
        inline bool has(usz index) const
        {
            return index < length;
        }

        // -------------------------------------------------------------------------
        // Iterator Interface (The Array *IS* the Iterator)
        // -------------------------------------------------------------------------

        // Returns a COPY of the array structure (cheap due to CoW).
        Array<T> begin() const { return *this; }

        Array<T> end() const { return Array<T>(); }

        bool operator!=(const Array<T> &other) const
        {
            return length > 0;
        }

        T &operator*()
        {
            return (*this)[0];
        }

        Array<T> &operator++()
        {
            shift();
            return *this;
        }

        // -------------------------------------------------------------------------
        // Core Mutations
        // -------------------------------------------------------------------------

        void alloc(usz fwd, usz back = 0)
        {
            if (fwd == 0 && back == 0)
                return;
            ArrayFragment<T> *f = ArrayFragment<T>::create(fwd, back);
            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.push(Xi::Move(u));
            recompute();
        }

        // Flatten to single contiguous C-array
        T *data()
        {
            if (length == 0)
                return null;
            if (uses.count == 1 && !uses[0].reverse)
                return uses[0].fragment->data;

            ArrayFragment<T> *flat = ArrayFragment<T>::create(length);
            usz w = 0;
            for (usz i = 0; i < uses.count; ++i)
            {
                ArrayUse<T> &u = uses[i];
                T *src = u.fragment->data;
                usz cnt = u.fragment->length;
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
            flat->length = length;

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

        void unshift(T t)
        {
            if (uses.count > 0 && (*uses[0].fragment->useCount == 1))
            {
                ArrayFragment<T> *f = uses[0].fragment;
                // Check Headroom
                if (!uses[0].reverse && f->head_cap() > 0)
                {
                    f->data--;
                    new (&f->data[0]) T(Xi::Move(t));
                    f->length++;
                    length++;
                    for (usz i = 1; i < uses.count; ++i)
                        uses[i].at++;
                    return;
                }
            }
            // Alloc new fragment with back-buffer preference
            ArrayFragment<T> *f = ArrayFragment<T>::create(1, 4);
            f->data--;
            new (&f->data[0]) T(Xi::Move(t));
            f->length = 1;

            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.insert(0, Xi::Move(u));
            recompute();
        }

        void push(T t)
        {
            if (uses.count > 0)
            {
                ArrayUse<T> &last = uses[uses.count - 1];
                if (*last.fragment->useCount == 1 && !last.reverse)
                {
                    if (last.fragment->tail_cap() > 0)
                    {
                        new (&last.fragment->data[last.fragment->length]) T(Xi::Move(t));
                        last.fragment->length++;
                        length++;
                        return;
                    }
                    else
                    {
                        // REALLOCATE current fragment with more headroom
                        usz newCap = last.fragment->capacity * 2;
                        ArrayFragment<T> *next = ArrayFragment<T>::create(newCap);
                        for (usz i = 0; i < last.fragment->length; ++i)
                        {
                            new (&next->data[i]) T(Xi::Move(last.fragment->data[i]));
                        }
                        next->length = last.fragment->length;
                        last.fragment->release();
                        last.fragment = next;

                        // Now push
                        new (&next->data[next->length]) T(Xi::Move(t));
                        next->length++;
                        length++;
                        return;
                    }
                }
            }
            // Fallback if shared or first push
            alloc(1, 4); // Alloc 1 with 4 headroom
            new (&uses[uses.count - 1].fragment->data[0]) T(Xi::Move(t));
            uses[uses.count - 1].fragment->length = 1;
            recompute();
        }

        // Returns the logical index of the first occurrence of 'needle' at or after 'start'.
        // Returns -1 if not found.
        long long find(const T &needle, usz start = 0) const
        {
            usz currentGlobal = 0;

            for (usz i = 0; i < uses.count; ++i)
            {
                const ArrayUse<T> &u = uses[i];
                usz len = u.fragment->length;
                usz endGlobal = currentGlobal + len;

                // Optimization: Skip entire fragments that come before the start index
                if (endGlobal <= start)
                {
                    currentGlobal += len;
                    continue;
                }

                // Determine local starting point within this fragment
                // If we are in the fragment containing 'start', offset is (start - currentGlobal)
                // Otherwise, we scan the whole fragment (offset 0)
                usz k = (currentGlobal < start) ? (start - currentGlobal) : 0;

                T *raw = u.fragment->data;

                for (; k < len; ++k)
                {
                    // Map logical index k to physical index
                    usz physicalIdx = u.reverse ? (len - 1 - k) : k;

                    if (raw[physicalIdx] == needle)
                    {
                        return (long long)(currentGlobal + k);
                    }
                }
                currentGlobal += len;
            }
            return -1;
        }

        // Zero-Copy Reverse
        // Creates a new Array view where fragments are ordered backward
        // and read backward.
        Array<T> reverse() const
        {
            Array<T> res;
            res.uses.reserve(uses.count);

            for (usz i = uses.count; i > 0; --i)
            {
                ArrayUse<T> u = uses[i - 1]; // Copy (Inc Ref)
                u.reverse = !u.reverse;      // Flip reading direction
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

            // Extract old info BEFORE mutation
            ArrayFragment<T> *old = uses[idx].fragment;
            bool isRev = uses[idx].reverse;
            usz oldAt = uses[idx].at;

            // Create 2 New Fragments (Physical Split)
            usz lenA = off;
            usz lenB = old->length - off;
            ArrayFragment<T> *fA = ArrayFragment<T>::create(lenA);
            ArrayFragment<T> *fB = ArrayFragment<T>::create(lenB);

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
                // Un-reverse during split for simplicity
                for (usz i = 0; i < lenA; ++i)
                    new (&fA->data[i]) T(src[old->length - 1 - i]);
                for (usz i = 0; i < lenB; ++i)
                    new (&fB->data[i]) T(src[lenB - 1 - i]);
            }
            fA->length = lenA;
            fB->length = lenB;

            // Release old use and Assign Part A
            uses[idx].fragment->release();
            uses[idx].fragment = fA;
            uses[idx].reverse = false;
            uses[idx].at = oldAt;

            // Insert Part B
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

            // Remove from this
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
            const ArrayUse<T> &u = uses[idx];
            if (u.reverse)
                return u.fragment->data[u.fragment->length - 1 - off];
            return u.fragment->data[off];
        }

        void pushEach(const T *ptr, usz cnt)
        {
            if (!ptr || cnt == 0)
                return;
            ArrayFragment<T> *f = ArrayFragment<T>::create(cnt);
            for (usz i = 0; i < cnt; ++i)
                new (&f->data[i]) T(ptr[i]);
            f->length = cnt;
            ArrayUse<T> u;
            u.fragment = f;
            u.own = true;
            uses.push(Xi::Move(u));
            length += cnt;
            // Optimization: recompute() not strictly needed if appending to end and maintaining length locally
            // but safer to keep state consistent.
            recompute();
        }

        void set(const Array<T> &o, usz idx)
        {
            for (usz i = 0; i < o.length; ++i)
            {
                if (idx + i < length)
                    (*this)[idx + i] = o[i];
                else
                    push(o[i]);
            }
        }

        // Add this inside the public section of class Array
        void remove(usz index)
        {
            if (index >= length)
                return;
            usz off;
            usz idx = find_use_index(index, off);

            if (idx != (usz)-1)
            {
                // If the fragment has more than 1 element, we must split or shrink it.
                // For simplicity and to maintain the "View" logic, it's often easier
                // to use your existing splice logic or break_at.
                splice(index, index + 1);
            }
        }

        void clear()
        {
            uses.clear();
            length = 0;
        }
    };

} // namespace Xi

#endif // XI_ARRAY_HPP