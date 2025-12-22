// src/Xi/Map.hpp

#ifndef XI_MAP_HPP
#define XI_MAP_HPP 1

#include "Array.hpp"
#include "String.hpp"
#include "Hasher.hpp"

namespace Xi
{

    // -----------------------------------------------------------------------------
    // Entry Container
    // -----------------------------------------------------------------------------
    template <typename K, typename V>
    struct MapEntry
    {
        K key;
        V value;
        u32 hash; // Cached hash (top 31 bits), low bit 0 = empty, 1 = occupied

        // distance is packed implicitly or calculated.
        // To implement Robin Hood strictly we need probe distance (PSL).
        // We will compute PSL on the fly to save memory (MapEntry size matters).

        bool isEmpty() const { return (hash & 1) == 0; }
        void markEmpty() { hash = 0; }
        void markOccupied(u32 h) { hash = (h | 1); } // Ensure LSB is 1
    };

    // -----------------------------------------------------------------------------
    // Map Class (Robin Hood Hash Table)
    // -----------------------------------------------------------------------------
    template <typename K, typename V>
    class Map
    {
    private:
        // Flat buffer of slots
        ArrayFragment<MapEntry<K, V>> *buckets;

        usz count;
        usz capacity;
        usz mask;
        usz threshold;

        // Load factor 0.85 (higher than chaining because Robin Hood handles collisions well)
        static constexpr usz MIN_CAPACITY = 8;

        // Hash helper: ensures LSB is 1 for occupied slots check
        static inline u32 clean_hash(usz h)
        {
            // Fold 64-bit hash to 32-bit if needed
            u32 h32 = (u32)h ^ (u32)(h >> 32);
            // Ensure MSB is 0 (reserved?) No, ensure LSB is 1.
            return (h32 | 1);
        }

        // Helper: Distance from ideal bucket (Probe Sequence Length)
        static inline usz dist(usz index, usz ideal, usz capMask)
        {
            return (index - ideal) & capMask;
        }

        void alloc_buckets(usz newCap)
        {
            // Use ArrayFragment primitive for raw memory management
            buckets = ArrayFragment<MapEntry<K, V>>::create(newCap);
            buckets->length = newCap; // We claim full length as addressed slots

            for (usz i = 0; i < newCap; ++i)
            {
                new (&buckets->data[i]) MapEntry<K, V>();
            }

            capacity = newCap;
            mask = newCap - 1;
            threshold = (newCap * 85) / 100; // 85% load
            count = 0;
        }

        void free_buckets()
        {
            if (buckets)
                buckets->release();
            buckets = null;
        }

        // Core Insertion Logic (Robin Hood)
        // Returns pointer to value (existing or new)

        bool insert_internal(MapEntry<K, V> *slots, usz capMask, K &&key, V &&val, bool overwrite)
        {
            usz hRaw = Hasher<K>::hash(key);
            u32 h = clean_hash(hRaw);
            usz idx = hRaw & capMask;
            usz psl = 0;

            MapEntry<K, V> entryToInsert;
            entryToInsert.key = Xi::Move(key);
            entryToInsert.value = Xi::Move(val);
            entryToInsert.markOccupied(h);

            while (true)
            {
                MapEntry<K, V> &slot = slots[idx];

                if (slot.isEmpty())
                {
                    new (&slot) MapEntry<K, V>(Xi::Move(entryToInsert));
                    return true; // New insertion
                }

                if (slot.hash == h && Equal<K>::eq(slot.key, entryToInsert.key))
                {
                    if (overwrite)
                        slot.value = Xi::Move(entryToInsert.value);
                    return false; // Existing key updated
                }

                // Optimization: Use slot.hash instead of re-hashing key!
                usz slotHome = (slot.hash) & capMask;
                usz slotPSL = (idx - slotHome) & capMask;

                if (psl > slotPSL)
                {
                    Xi::Swap(entryToInsert, slot);
                    psl = slotPSL;
                }

                idx = (idx + 1) & capMask;
                psl++;
            }
        }

        void resize(usz newCap)
        {
            ArrayFragment<MapEntry<K, V>> *oldBuckets = buckets;
            usz oldCap = capacity;

            // Alloc new zeroed buffer
            alloc_buckets(newCap); // Sets count=0, capacity=newCap

            // Rehash all items
            if (oldBuckets)
            {
                for (usz i = 0; i < oldCap; ++i)
                {
                    MapEntry<K, V> &e = oldBuckets->data[i];
                    if (!e.isEmpty())
                    {
                        insert_internal(buckets->data, mask, Xi::Move(e.key), Xi::Move(e.value), true);
                    }
                }
                oldBuckets->release(); // Release old memory
            }
        }

    public:
        // -------------------------------------------------------------------------
        // Constructors
        // -------------------------------------------------------------------------
        Map() : buckets(null), count(0), capacity(0)
        {
            alloc_buckets(MIN_CAPACITY);
        }

        Map(const Map &other) : buckets(null), count(0), capacity(0)
        {
            alloc_buckets(other.capacity);
            // Deep copy items
            for (usz i = 0; i < other.capacity; ++i)
            {
                if (!other.buckets->data[i].isEmpty())
                {
                    set(other.buckets->data[i].key, other.buckets->data[i].value);
                }
            }
        }

        Map(Map &&other)
        {
            buckets = other.buckets;
            count = other.count;
            capacity = other.capacity;
            mask = other.mask;
            threshold = other.threshold;

            other.buckets = null;
            other.count = 0;
            other.capacity = 0;
        }

        Map &operator=(Map &&other)
        {
            if (this != &other)
            {
                free_buckets();
                buckets = other.buckets;
                count = other.count;
                capacity = other.capacity;
                mask = other.mask;
                threshold = other.threshold;
                other.buckets = null;
                other.count = 0;
            }
            return *this;
        }

        ~Map()
        {
            free_buckets();
        }

        // -------------------------------------------------------------------------
        // Core API
        // -------------------------------------------------------------------------

        usz size() const { return count; }

        // Insert or Update
        void set(const K &key, const V &val)
        {
            // Optimization: Create temp copies only once
            K k = key;
            V v = val;
            put(Xi::Move(k), Xi::Move(v));
        }

        void put(K key, V val)
        {
            if (count >= threshold)
                resize(capacity * 2);
            if (insert_internal(buckets->data, mask, Xi::Move(key), Xi::Move(val), true))
            {
                count++; // Only increment if it's actually a new entry!
            }
        }

        // Get Value (pointer to avoid copy, null if not found)
        V *get(const K &key)
        {
            if (count == 0)
                return null;
            usz hRaw = Hasher<K>::hash(key);
            u32 h = clean_hash(hRaw);
            usz idx = hRaw & mask;
            usz dist = 0;

            while (true)
            {
                MapEntry<K, V> &slot = buckets->data[idx];

                if (slot.isEmpty())
                    return null;

                // Optimization: Check cached hash first
                if (slot.hash == h && Equal<K>::eq(slot.key, key))
                {
                    return &slot.value;
                }

                // Stop probing if we traveled further than the item in current slot
                // (Robin Hood invariant)
                usz slotHome = Hasher<K>::hash(slot.key) & mask;
                usz slotDist = (idx - slotHome) & mask;
                if (dist > slotDist)
                    return null;

                idx = (idx + 1) & mask;
                dist++;
            }
        }

        const V *get(const K &key) const
        {
            return const_cast<Map *>(this)->get(key);
        }

        bool has(const K &key) const
        {
            return get(key) != null;
        }

        // Operator[] Access (Inserts default if missing)
        V &operator[](const K &key)
        {
            V *existing = get(key);
            if (existing)
                return *existing;

            put(key, V());
            return *get(key);
        }

        // Remove
        bool remove(const K &key)
        {
            if (count == 0)
                return false;
            usz hRaw = Hasher<K>::hash(key);
            u32 h = clean_hash(hRaw);
            usz idx = hRaw & mask;
            usz dist = 0;

            while (true)
            {
                MapEntry<K, V> &slot = buckets->data[idx];
                if (slot.isEmpty())
                    return false;

                usz slotHome = Hasher<K>::hash(slot.key) & mask;
                usz slotDist = (idx - slotHome) & mask;

                if (dist > slotDist)
                    return false;

                if (slot.hash == h && Equal<K>::eq(slot.key, key))
                {
                    // Found! Now remove using backward shift compaction
                    // (Better than tombstones for cache)
                    slot.value.~V();
                    slot.key.~K();
                    slot.markEmpty();
                    count--;

                    // Shift following items back if they are not at their home bucket
                    usz nextIdx = (idx + 1) & mask;
                    while (true)
                    {
                        MapEntry<K, V> &nextSlot = buckets->data[nextIdx];
                        if (nextSlot.isEmpty())
                            break;

                        usz nextHome = Hasher<K>::hash(nextSlot.key) & mask;
                        usz distFromHome = (nextIdx - nextHome) & mask;

                        if (distFromHome == 0)
                            break; // It's home, stop shifting

                        // Move back to 'idx'
                        new (&buckets->data[idx]) MapEntry<K, V>(Xi::Move(nextSlot));
                        nextSlot.markEmpty(); // Technically destructed by Move

                        idx = nextIdx;
                        nextIdx = (nextIdx + 1) & mask;
                    }
                    return true;
                }

                idx = (idx + 1) & mask;
                dist++;
            }
        }

        // -------------------------------------------------------------------------
        // Iterator
        // -------------------------------------------------------------------------
        struct Iterator
        {
            MapEntry<K, V> *ptr;
            MapEntry<K, V> *endPtr;

            Iterator(MapEntry<K, V> *p, MapEntry<K, V> *e) : ptr(p), endPtr(e)
            {
                // Fast forward to first occupied
                if (ptr && ptr < endPtr && ptr->isEmpty())
                    ++(*this);
            }

            bool operator!=(const Iterator &o) const { return ptr != o.ptr; }

            Iterator &operator++()
            {
                do
                {
                    ptr++;
                } while (ptr < endPtr && ptr->isEmpty());
                return *this;
            }

            // Return a pair-like struct or just the key/val?
            // Standard is pair, but we want simplicity.
            // Return reference to Entry to allow .key / .value access
            MapEntry<K, V> &operator*() { return *ptr; }
            MapEntry<K, V> *operator->() { return ptr; }
        };

        Iterator begin() { return Iterator(buckets->data, buckets->data + capacity); }
        Iterator end() { return Iterator(buckets->data + capacity, buckets->data + capacity); }

        // Const Iterator
        struct ConstIterator
        {
            const MapEntry<K, V> *ptr;
            const MapEntry<K, V> *endPtr;
            ConstIterator(const MapEntry<K, V> *p, const MapEntry<K, V> *e) : ptr(p), endPtr(e)
            {
                if (ptr && ptr < endPtr && ptr->isEmpty())
                    ++(*this);
            }
            bool operator!=(const ConstIterator &o) const { return ptr != o.ptr; }
            ConstIterator &operator++()
            {
                do
                {
                    ptr++;
                } while (ptr < endPtr && ptr->isEmpty());
                return *this;
            }
            const MapEntry<K, V> &operator*() const { return *ptr; }
            const MapEntry<K, V> *operator->() const { return ptr; }
        };

        ConstIterator begin() const { return ConstIterator(buckets->data, buckets->data + capacity); }
        ConstIterator end() const { return ConstIterator(buckets->data + capacity, buckets->data + capacity); }

        // Inside class Map public:

        void clear()
        {
            if (count == 0)
                return;

            for (usz i = 0; i < capacity; ++i)
            {
                MapEntry<K, V> &slot = buckets->data[i];
                if (!slot.isEmpty())
                {
                    // Manually call destructors for the objects in the slot
                    slot.key.~K();
                    slot.value.~V();
                    slot.markEmpty();
                }
            }
            count = 0;
        }

        // -------------------------------------------------------------------------
        // Debug / Info
        // -------------------------------------------------------------------------
        Array<K> keys() const
        {
            Array<K> k;
            k.alloc(count);
            for (auto &e : *this)
                k.push(e.key);
            return k;
        }
    };

} // namespace Xi

#endif // XI_MAP_HPP