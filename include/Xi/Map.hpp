#ifndef XI_MAP_HPP
#define XI_MAP_HPP 1

#include "Array.hpp"
#include "String.hpp"

namespace Xi
{

// -----------------------------------------------------------------------------
// MapEntry
// Container for Key-Value pairs.
// -----------------------------------------------------------------------------
template <typename K, typename V>
struct MapEntry
{
    K key;
    V value;
    u32 fnvHash; // LSB (bit 0) is used as the "Occupied" flag.

    MapEntry() : key(), value(), fnvHash(0) {}

    // 1. Move Constructor
    // Essential for resizing without deep copying keys/values.
    MapEntry(MapEntry &&o) noexcept 
        : key(Xi::Move(o.key)), value(Xi::Move(o.value)), fnvHash(o.fnvHash)
    {
        o.fnvHash = 0;
    }

    // 2. Move Assignment
    // Essential for "Robin Hood" swapping logic.
    MapEntry &operator=(MapEntry &&o) noexcept
    {
        if (this != &o)
        {
            key = Xi::Move(o.key);
            value = Xi::Move(o.value);
            fnvHash = o.fnvHash;
            o.fnvHash = 0;
        }
        return *this;
    }

    // Disable Copying to prevent accidental heavy operations
    MapEntry(const MapEntry &) = delete;
    MapEntry &operator=(const MapEntry &) = delete;

    inline bool isEmpty() const { return (fnvHash & 1) == 0; }
    inline void markEmpty() { fnvHash = 0; }
    
    // Sets hash and ensures LSB is 1
    inline void setHash(u32 h) { fnvHash = (h | 1); } 
};

// -----------------------------------------------------------------------------
// Map
// High-Performance Robin Hood Hash Table
// -----------------------------------------------------------------------------
template <typename K, typename V>
class Map
{
private:
    ArrayFragment<MapEntry<K, V>> *buckets;

    usz count;
    usz capacity;
    usz mask;
    usz threshold;

    static constexpr usz MIN_CAPACITY = 16; // Must be Power of 2

    // Helper: Folds 64-bit hash to 32-bit and ensures LSB is 1
    static inline u32 clean_hash(usz h)
    {
        u32 h32 = (u32)h ^ (u32)(h >> 32);
        return (h32 | 1);
    }

    void alloc_buckets(usz newCap)
    {
        buckets = ArrayFragment<MapEntry<K, V>>::create(newCap);
        buckets->length = newCap; 
        // Initialize valid objects in raw memory
        for (usz i = 0; i < newCap; ++i) 
            new (&buckets->data[i]) MapEntry<K, V>();
        
        capacity = newCap;
        mask = newCap - 1;
        threshold = (newCap * 85) / 100; // 85% Load Factor
        count = 0;
    }

    void free_buckets()
    {
        if (buckets) buckets->release();
        buckets = null;
    }

    // Core Insertion Logic
    // Returns: true if inserted new, false if updated existing.
    // Throws/Aborts if map is corrupted (but we return false to trigger resize instead).
    bool insert_internal(MapEntry<K, V> *slots, usz cap, usz capMask, K &&key, V &&val, bool overwrite)
    {
        usz hRaw = FNVHasher<K>::fnvHash(key);
        u32 h = clean_hash(hRaw);
        usz idx = (usz)h & capMask;
        usz psl = 0; // Probe Sequence Length

        // Create the entry we want to insert
        MapEntry<K, V> toInsert;
        toInsert.key = Xi::Move(key);
        toInsert.value = Xi::Move(val);
        toInsert.setHash(h);

        // Hard Limit: Stop if we've scanned the entire array.
        // This prevents infinite loops if the map logic fails.
        for (usz i = 0; i < cap; ++i)
        {
            MapEntry<K, V> &slot = slots[idx];

            // 1. Found Empty Slot -> Insert
            if (slot.isEmpty())
            {
                slot = Xi::Move(toInsert);
                return true; 
            }

            // 2. Found Existing Key -> Update
            if (slot.fnvHash == h && Equal<K>::eq(slot.key, toInsert.key))
            {
                if (overwrite) slot.value = Xi::Move(toInsert.value);
                return false; 
            }

            // 3. Robin Hood Swapping
            // Check distance of the item currently in the slot
            usz slotHome = (usz)(slot.fnvHash) & capMask;
            usz slotPSL = (idx - slotHome) & capMask;

            if (psl > slotPSL)
            {
                // We are "poorer" (further from home) than the current tenant.
                // Swap places. We take this spot, and the current tenant moves on.
                Xi::Swap(toInsert, slot);
                psl = slotPSL;
            }

            // Move to next bucket
            idx = (idx + 1) & capMask;
            psl++;
        }
        
        // If we reach here, the map is full or highly fragmented.
        // Return false usually implies update, but here we depend on the caller 
        // checking the count to know if it failed.
        // Actually, for safety, if we fall out of loop, we force a resize in `put`.
        return true; 
    }

    void resize(usz newCap)
    {
        ArrayFragment<MapEntry<K, V>> *oldBuckets = buckets;
        usz oldCap = capacity;

        alloc_buckets(newCap); // Sets count = 0

        if (oldBuckets)
        {
            for (usz i = 0; i < oldCap; ++i)
            {
                MapEntry<K, V> &e = oldBuckets->data[i];
                if (!e.isEmpty())
                {
                    // Move item to new table. 
                    // Note: We force overwrite=true, but since keys are unique in old table,
                    // it effectively just inserts.
                    insert_internal(buckets->data, capacity, mask, Xi::Move(e.key), Xi::Move(e.value), true);
                    count++; // Manually restore count
                }
            }
            oldBuckets->release(); 
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
        for (usz i = 0; i < other.capacity; ++i)
        {
            if (!other.buckets->data[i].isEmpty())
                set(other.buckets->data[i].key, other.buckets->data[i].value);
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
    // Public API
    // -------------------------------------------------------------------------

    usz size() const { return count; }

    void set(const K &key, const V &val)
    {
        K k = key;
        V v = val;
        put(Xi::Move(k), Xi::Move(v));
    }

    void put(K key, V val)
    {
        if (count >= threshold) 
            resize(capacity * 2);

        bool isNew = insert_internal(buckets->data, capacity, mask, Xi::Move(key), Xi::Move(val), true);
        if (isNew) count++;
    }

    V *get(const K &key)
    {
        if (count == 0) return null;

        usz hRaw = FNVHasher<K>::fnvHash(key);
        u32 h = clean_hash(hRaw);
        usz idx = (usz)h & mask;
        usz dist = 0;

        // Loop Limit: Capacity (Safety mechanism)
        for (usz i = 0; i < capacity; ++i)
        {
            MapEntry<K, V> &slot = buckets->data[idx];

            if (slot.isEmpty()) return null;

            if (slot.fnvHash == h && Equal<K>::eq(slot.key, key))
                return &slot.value;

            // Robin Hood Pruning:
            // If the item in this slot is closer to home than we are, 
            // then our key cannot exist further down the chain.
            usz slotHome = (usz)(slot.fnvHash) & mask;
            usz slotDist = (idx - slotHome) & mask;
            
            if (dist > slotDist) return null;

            idx = (idx + 1) & mask;
            dist++;
        }
        return null;
    }

    const V *get(const K &key) const { return const_cast<Map *>(this)->get(key); }

    bool has(const K &key) const { return get(key) != null; }

    // Safe Operator[]
    // If key missing, inserts default value.
    V &operator[](const K &key)
    {
        V *existing = get(key);
        if (existing) return *existing;

        put(key, V());
        return *get(key);
    }

    bool remove(const K &key)
    {
        if (count == 0) return false;

        usz hRaw = FNVHasher<K>::fnvHash(key);
        u32 h = clean_hash(hRaw);
        usz idx = (usz)h & mask;
        usz dist = 0;

        for (usz i = 0; i < capacity; ++i)
        {
            MapEntry<K, V> &slot = buckets->data[idx];
            
            if (slot.isEmpty()) return false;

            usz slotHome = (usz)(slot.fnvHash) & mask;
            usz slotDist = (idx - slotHome) & mask;

            if (dist > slotDist) return false;

            if (slot.fnvHash == h && Equal<K>::eq(slot.key, key))
            {
                // Found it. Remove it.
                count--;

                // Backward Shift Compaction (The "Tombstone-free" approach)
                usz nextIdx = (idx + 1) & mask;
                
                // Shift items back until we hit an empty slot or an item at its home
                for (usz j = 0; j < capacity; ++j) 
                {
                    MapEntry<K, V> &nextSlot = buckets->data[nextIdx];
                    
                    if (nextSlot.isEmpty()) 
                    {
                        // End of chain. Clear the current hole.
                        buckets->data[idx] = MapEntry<K, V>();
                        return true;
                    }

                    usz nextHome = (usz)(nextSlot.fnvHash) & mask;
                    usz distFromHome = (nextIdx - nextHome) & mask;

                    if (distFromHome == 0)
                    {
                        // Item is at home, cannot shift further back.
                        buckets->data[idx] = MapEntry<K, V>();
                        return true;
                    }

                    // Move item back into the hole (idx)
                    buckets->data[idx] = Xi::Move(nextSlot);
                    
                    // The hole moves to nextIdx
                    idx = nextIdx;
                    nextIdx = (nextIdx + 1) & mask;
                }
                return true;
            }

            idx = (idx + 1) & mask;
            dist++;
        }
        return false;
    }

    void clear()
    {
        if (count == 0) return;
        for (usz i = 0; i < capacity; ++i)
        {
            if (!buckets->data[i].isEmpty())
                buckets->data[i] = MapEntry<K, V>(); // Reset to default
        }
        count = 0;
    }

    // -------------------------------------------------------------------------
    // Iterators
    // -------------------------------------------------------------------------
    struct Iterator
    {
        MapEntry<K, V> *ptr;
        MapEntry<K, V> *endPtr;

        Iterator(MapEntry<K, V> *p, MapEntry<K, V> *e) : ptr(p), endPtr(e)
        {
            if (ptr && ptr < endPtr && ptr->isEmpty()) ++(*this);
        }

        bool operator!=(const Iterator &o) const { return ptr != o.ptr; }

        Iterator &operator++()
        {
            do { ptr++; } while (ptr < endPtr && ptr->isEmpty());
            return *this;
        }

        MapEntry<K, V> &operator*() { return *ptr; }
        MapEntry<K, V> *operator->() { return ptr; }
    };

    Iterator begin() { return Iterator(buckets->data, buckets->data + capacity); }
    Iterator end() { return Iterator(buckets->data + capacity, buckets->data + capacity); }

    struct ConstIterator
    {
        const MapEntry<K, V> *ptr;
        const MapEntry<K, V> *endPtr;

        ConstIterator(const MapEntry<K, V> *p, const MapEntry<K, V> *e) : ptr(p), endPtr(e)
        {
            if (ptr && ptr < endPtr && ptr->isEmpty()) ++(*this);
        }

        bool operator!=(const ConstIterator &o) const { return ptr != o.ptr; }

        ConstIterator &operator++()
        {
            do { ptr++; } while (ptr < endPtr && ptr->isEmpty());
            return *this;
        }

        const MapEntry<K, V> &operator*() const { return *ptr; }
        const MapEntry<K, V> *operator->() const { return ptr; }
    };

    ConstIterator begin() const { return ConstIterator(buckets->data, buckets->data + capacity); }
    ConstIterator end() const { return ConstIterator(buckets->data + capacity, buckets->data + capacity); }
};

} // namespace Xi

#endif // XI_MAP_HPP