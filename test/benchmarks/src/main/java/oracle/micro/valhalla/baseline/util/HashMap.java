/*
 * Copyright (c) 2016, 2018, Oracle and/or its affiliates. All rights reserved.
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This code is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 only, as
 * published by the Free Software Foundation.
 *
 * This code is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
 * version 2 for more details (a copy is included in the LICENSE file that
 * accompanied this code).
 *
 * You should have received a copy of the GNU General Public License version
 * 2 along with this work; if not, write to the Free Software Foundation,
 * Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301 USA.
 *
 * Please contact Oracle, 500 Oracle Parkway, Redwood Shores, CA 94065 USA
 * or visit www.oracle.com if you need additional information or have any
 * questions.
 */
package oracle.micro.valhalla.baseline.util;

import oracle.micro.valhalla.util.AbstractMap;
import oracle.micro.valhalla.util.Cursor;

import java.util.*;

/*
 * Baseline (ordinary) HashMap with Iterator and Cursor
 */
public class HashMap<K, V> extends AbstractMap<K, V> {

    Entry<K, V>[] table;

    public HashMap(int initialCapacity) {
        super();
        if (initialCapacity < 0)
            throw new IllegalArgumentException("Illegal initial capacity: " +
                    initialCapacity);
        if (initialCapacity > MAXIMUM_CAPACITY)
            initialCapacity = MAXIMUM_CAPACITY;

        // Find a power of 2 >= initialCapacity
        int capacity = tableSizeFor(initialCapacity);
        threshold = (int) (capacity * LOAD_FACTOR);
        @SuppressWarnings({"rawtypes", "unchecked"})
        Entry<K, V>[] newTab = (Entry<K, V>[]) new Entry[capacity];
        table = newTab;
    }

    public HashMap() {
        this(DEFAULT_INITIAL_CAPACITY);
    }

    static class Entry<K, V> implements Map.Entry<K, V> {
        final K key;
        V value;
        Entry<K, V> next;
        final int hash;

        Entry(int h, K k, V v, Entry<K, V> n) {
            value = v;
            next = n;
            key = k;
            hash = h;
        }

        public final K getKey() {
            return key;
        }

        public final V getValue() {
            return value;
        }

        public final V setValue(V newValue) {
            throw new UnsupportedOperationException();
        }

        public final boolean equals(Object o) {
            if (!(o instanceof Map.Entry))
                return false;
            Map.Entry e = (Map.Entry) o;
            return Objects.equals(getKey(), e.getKey()) && Objects.equals(getValue(), e.getValue());
        }

        public final int hashCode() {
            return Objects.hashCode(key) ^ Objects.hashCode(value);
        }

        public final String toString() {
            return getKey() + "=" + getValue();
        }

    }

    //############################################
    @Override
    public V get(K key) {
        int hash = hash(key);
        for (Entry<K, V> e = table[indexFor(hash, table.length)];
             e != null;
             e = e.next) {
            if (e.hash == hash && Objects.equals(key, e.key))
                return e.value;
        }
        return null;
    }

    @Override
    public V put(K key, V value) {
        int hash = hash(key);
        int i = indexFor(hash, table.length);
        for (Entry<K,V> e = table[i]; e != null; e = e.next) {
            if (e.hash == hash && Objects.equals(key, e.key)) {
                V oldValue = e.value;
                e.value = value;
                return oldValue;
            }
        }

        modCount++;
        Entry<K,V> e = table[i];
        table[i] = new Entry<>(hash, key, value, e);
        if (size++ >= threshold)
            resize(2 * table.length);
        return null;
    }

    void resize(int newCapacity) {
        Entry<K,V>[] oldTable = table;
        int oldCapacity = oldTable.length;
        if (oldCapacity == MAXIMUM_CAPACITY) {
            threshold = Integer.MAX_VALUE;
            return;
        }
        @SuppressWarnings({"rawtypes","unchecked"})
        Entry<K,V>[] newTable = (Entry<K,V>[])new Entry[newCapacity];
        transfer(newTable);
        table = newTable;
        threshold = (int)(newCapacity * LOAD_FACTOR);
    }

    /**
     * Transfers all entries from current table to newTable.
     */
    void transfer(Entry<K,V>[] newTable) {
        Entry<K,V>[] src = table;
        int newCapacity = newTable.length;
        for (int j = 0; j < src.length; j++) {
            Entry<K,V> e = src[j];
            if (e != null) {
                src[j] = null;
                do {
                    Entry<K,V> next = e.next;
                    int i = indexFor(e.hash, newCapacity);
                    e.next = newTable[i];
                    newTable[i] = e;
                    e = next;
                } while (e != null);
            }
        }
    }

    @Override
    public V remove(K key) {
        int hash = hash(key);
        int i = indexFor(hash, table.length);
        Entry<K,V> prev = null;
        Entry<K,V> e = table[i];

        while (e != null) {
            Entry<K,V> next = e.next;
            if (e.hash == hash && Objects.equals(key, e.key)) {
                modCount++;
                size--;
                if (prev == null)
                    table[i] = next;
                else
                    prev.next = next;
                return e.value;
            }
            prev = e;
            e = next;
        }
        return null;
    }

    private static int getFilledSlot(Entry[] t, int from) {
        while (from < t.length && t[from] == null) {
            from++;
        }
        return from;
    }

    private class HashIterator  {
        Entry<K,V> next;        // next entry to return
        final int expectedModCount;   // For fast-fail
        int index;              // current slot

        HashIterator() {
            expectedModCount = modCount;
            if (size > 0) { // advance to first entry
                Entry<K,V>[] t = table;
                int from = getFilledSlot(t, 0);
                index = from;
                next = (from < t.length) ? t[from] : null;
            }
        }

        public boolean hasNext() {
            return next != null;
        }

        public Entry<K,V> nextEntry() {
            if (modCount != expectedModCount)
                throw new ConcurrentModificationException();
            Entry<K,V> e = next;
            if (e == null)
                throw new NoSuchElementException();

            if ((next = e.next) == null) {
                Entry<K,V>[] t = table;
                int from = getFilledSlot(t, index + 1);
                index = from;
                next = (from < t.length) ? t[from] : null;
            }
            return e;
        }

        public void remove() {
            throw new UnsupportedOperationException();
        }

    }

    private class EntryIterator extends HashIterator implements Iterator<Map.Entry<K, V>> {

        public Map.Entry<K, V> next() {
            return nextEntry();
        }
    }

    private class KeyIterator extends HashIterator implements Iterator<K> {

        public K next() {
            return nextEntry().key;
        }
    }

    private class ValueIterator extends HashIterator implements Iterator<V> {

        public V next() {
            return nextEntry().value;
        }
    }

    @Override
    public Iterator<K> keyIterator() {
        return new KeyIterator();
    }

    @Override
    public Iterator<V> valueIterator() {
        return new ValueIterator();
    }

    @Override
    public Iterator<Map.Entry<K, V>> entryIterator() {
        return new EntryIterator();
    }

    @Override
    public Cursor<Map.Entry<K, V>> entryCursor() {
        return EntryCursor.of(this);
    }

    @Override
    public Cursor<K> keyCursor() {
        return KeyCursor.of(this);
    }

    public static class EntryCursor<K,V> implements Cursor<Map.Entry<K,V>> {
        private final HashMap<K,V> map;
        private final int expectedModCount;
        private final int index;
        private final Entry<K,V> current;

        public static <K,V> EntryCursor<K,V> of(HashMap<K,V> map) {
            if (map.size > 0) { // advance to first entry
                Entry<K, V>[] t = map.table;
                int from = getFilledSlot(t, 0);
                return new EntryCursor<K, V>(map, map.modCount, from, t[from]);
            } else {
                return new EntryCursor<K, V>(map, map.modCount, 0, null);
            }
        }

        EntryCursor(HashMap<K, V> map, int expectedModCount, int index, Entry<K, V> current) {
            this.map = map;
            this.expectedModCount = expectedModCount;
            this.index = index;
            this.current = current;
        }

        public boolean hasElement() {
            return current != null;
        }

        public Map.Entry<K, V> get() {
            return current;
        }

        public Cursor<Map.Entry<K, V>> next() {
            return nextEntry();
        }

        public EntryCursor<K, V> nextEntry() {
            if(current == null)
                throw new NoSuchElementException();
            if (map.modCount != expectedModCount)
                throw new ConcurrentModificationException();
            Entry<K,V> e = current;
            if (e.next == null) {
                Entry<K, V>[] t = map.table;
                int from = getFilledSlot(t, index + 1);
                return new EntryCursor<K, V>(map, expectedModCount, from, (from < t.length) ? t[from] : null);
            } else {
                return new EntryCursor<K, V>(map, expectedModCount, index, e.next);

            }
        }
    }

    public static class KeyCursor<K,V> implements Cursor<K> {
        private final HashMap<K,V> map;
        private final int expectedModCount;
        private final int index;
        private final Entry<K,V> current;

        public static <K,V> KeyCursor<K,V> of(HashMap<K,V> map) {
            if (map.size > 0) { // advance to first entry
                Entry<K, V>[] t = map.table;
                int from = getFilledSlot(t, 0);
                return new KeyCursor<K, V>(map, map.modCount, from, t[from]);
            } else {
                return new KeyCursor<K, V>(map, map.modCount, 0, null);
            }
        }

        KeyCursor(HashMap<K, V> map, int expectedModCount, int index, Entry<K, V> current) {
            this.map = map;
            this.expectedModCount = expectedModCount;
            this.index = index;
            this.current = current;
        }

        public boolean hasElement() {
            return current != null;
        }

        public K get() {
            return current.key;
        }

        public Cursor<K> next() {
            return nextEntry();
        }

        public KeyCursor<K, V> nextEntry() {
            if(current == null)
                throw new NoSuchElementException();
            if (map.modCount != expectedModCount)
                throw new ConcurrentModificationException();
            Entry<K,V> e = current;
            if (e.next == null) {
                Entry<K, V>[] t = map.table;
                int from = getFilledSlot(t, index + 1);
                return new KeyCursor<K, V>(map, expectedModCount, from, (from < t.length) ? t[from] : null);
            } else {
                return new KeyCursor<K, V>(map, expectedModCount, index, e.next);

            }
        }
    }

}