//
// Copyright (c) 2023 ZettaScale Technology
//
// This program and the accompanying materials are made available under the
// terms of the Eclipse Public License 2.0 which is available at
// http://www.eclipse.org/legal/epl-2.0, or the Apache License, Version 2.0
// which is available at https://www.apache.org/licenses/LICENSE-2.0.
//
// SPDX-License-Identifier: EPL-2.0 OR Apache-2.0
//
// Contributors:
//   ZettaScale Zenoh Team, <zenoh@zettascale.tech>

//
// This file contains structures and classes API without implementations
//

#pragma once

#include "base.hxx"
#include "internal.hxx"
#include <cstddef>
#include <cstdint>
#include <string_view>
#include <string>
#include <utility>
#include <vector>

namespace zenoh {

class ZenohCodec;

/// @brief A Zenoh serialized data representation
class Bytes : public Owned<::z_owned_bytes_t> {
public:
    using Owned::Owned;

    /// @name Methods
     
    /// @brief Constructs a shallow copy of this data
    Bytes clone() const {
        Bytes b(nullptr);
        ::z_bytes_clone(this->loan(), &b._0);
        return b; 
    }

    /// @brief Get number of bytes in the pyload.
    size_t size() const {
        return ::z_bytes_len(this->loan());
    }

    /// @brief Serialize specified type.
    ///
    /// @tparam T Type to serialize
    /// @tparam Codec Codec to use
    /// @param data Instance of T to serialize
    /// @param codec Instance of Codec to use
    /// @return Serialized data
    template<class T, class Codec = ZenohCodec>
    static Bytes serialize(T data, Codec codec = Codec()) {
        return codec.serialize(data);
    }

    template<class ForwardIt, class Codec = ZenohCodec> 
    static Bytes serialize_from_iter(ForwardIt begin, ForwardIt end, Codec codec = Codec()) {
        struct BytesEncodeIteratorBody {
            ForwardIt current;
            ForwardIt end;
            Codec codec;
            static void encode(z_owned_bytes_t* b, void* context) {
                BytesEncodeIteratorBody* s = reinterpret_cast<BytesEncodeIteratorBody*>(context);
                if (s->current == s->end) {
                    ::z_null(b);
                    return;
                }
                *b = Bytes::serialize(*s->current, s->codec).take();
                s->current++;
            }
        };

        Bytes b(nullptr);
        BytesEncodeIteratorBody body = {.current = begin, .end = end , .codec = codec };
        
        ::z_bytes_encode_from_iter(detail::as_owned_c_ptr(b), &BytesEncodeIteratorBody::encode, reinterpret_cast<void*>(&body));
        return b;
    }

    /// @brief Deserialize into specified type.
    ///
    /// @tparam T Type to deserialize into
    /// @tparam Codec Codec to use
    /// @return Deserialzied data
    template<class T, class Codec = ZenohCodec>
    T deserialize(ZError* err = nullptr, Codec codec = Codec()) const {
        return codec.template deserialize<T>(*this, err);
    }

    class Iterator;

    /// @brief Get iterator to multi-element data.
    /// @return Iterator over multiple elements of data
    Iterator iter() const;

    class BytesReader : public Copyable<::z_bytes_reader_t> {
    public:
        using Copyable::Copyable;

        size_t read(uint8_t* buf, size_t len) {
            return ::z_bytes_reader_read(&this->_0, buf, len);
        }

        int64_t position() {
            return ::z_bytes_reader_tell(&this->_0);
        }


        void seek_from_current(int64_t offset, ZError* err = nullptr) {
            __ZENOH_ERROR_CHECK(
                ::z_bytes_reader_seek(&this->_0, offset, SEEK_CUR),
                err,
                "seek_from_current failed"
            );
        }

        void seek_from_start(int64_t offset, ZError* err = nullptr) {
            __ZENOH_ERROR_CHECK(
                ::z_bytes_reader_seek(&this->_0, offset, SEEK_SET),
                err,
                "seek_from_start failed"
            );
        }

        void seek_from_end(int64_t offset, ZError* err = nullptr) {
            __ZENOH_ERROR_CHECK(
                ::z_bytes_reader_seek(&this->_0, offset, SEEK_END),
                err,
                "seek_from_end failed"
            );
        }
    };

    /// @brief Create data reader
    /// @return 
    BytesReader reader() const {
        return BytesReader(::z_bytes_get_reader(this->loan()));
    }
};

class Bytes::Iterator: Copyable<::z_bytes_iterator_t> {
public:
    using Copyable::Copyable;
    Bytes next() {
        Bytes b(nullptr);
        ::z_bytes_iterator_next(&this->_0, detail::as_owned_c_ptr(b));
        return b;
    }
};

Bytes::Iterator Bytes::iter() const {
    return Bytes::Iterator(::z_bytes_get_iterator(this->loan()));
}




namespace detail {

template<class T> 
struct ZenohDeserializer {};

template<>
struct ZenohDeserializer<std::string> {
    static std::string deserialize(const Bytes& b, ZError* err = nullptr) {
        auto reader = b.reader();
        std::string s(b.size(), '0');
        reader.read(reinterpret_cast<uint8_t*>(s.data()), s.size());
        return s;
    }
};

template<>
struct ZenohDeserializer<std::vector<uint8_t>> {
    static std::vector<uint8_t> deserialize(const Bytes& b, ZError* err = nullptr) {
        auto reader = b.reader();
        std::vector<uint8_t> v(b.size());
        reader.read(v.data(), b.size());
        return v;
    }
};

template<class A, class B>
struct ZenohDeserializer<std::pair<A, B>> {
    static std::pair<A, B> deserialize(const Bytes& b, ZError* err = nullptr) {
        zenoh::Bytes ba(nullptr), bb(nullptr);
        __ZENOH_ERROR_CHECK(
            ::z_bytes_decode_into_pair(detail::loan(b), detail::as_owned_c_ptr(ba), detail::as_owned_c_ptr(bb)),
            err,
            "Failed to deserialize into std::pair"
        );
        return {ZenohDeserializer<A>::deserialize(ba, err), ZenohDeserializer<B>::deserialize(bb, err)};
    }
};

template<class T>
struct ZenohDeserializer<std::vector<T>> {
    static std::vector<T> deserialize(const Bytes& b, ZError* err = nullptr) {
        std::vector<T> v;
        auto it = b.iter();
        for (auto bb = it.next(); static_cast<bool>(bb); bb = it.next()) {
            v.push_back(bb.deserialize<T>(err));
        }
        
        return v;
    }
};

template<class K, class V>
struct ZenohDeserializer<std::unordered_map<K, V>> {
    static std::unordered_map<K, V> deserialize(const Bytes& b, ZError* err = nullptr) {
        std::unordered_map<K, V> m;
        auto it = b.iter();
        for (auto bb = it.next(); static_cast<bool>(bb); bb = it.next()) {
            m.insert(bb.deserialize<std::pair<K, V>>(err));
        }
        
        return m;
    }
};

}

struct ZenohCodec {
    // template<class T>
    // static Bytes serialize(T t);

    static Bytes serialize(std::string_view s) {
        return ZenohCodec::serialize(std::make_pair(reinterpret_cast<const uint8_t*>(s.data()), s.size()));
    }

    static Bytes serialize(const std::pair<const uint8_t*, size_t>& s) {
        Bytes b(nullptr);
        ::z_bytes_encode_from_slice(detail::as_owned_c_ptr(b), s.first, s.second);
        return b;
    }

    static Bytes serialize(const std::vector<uint8_t>& s) {
        Bytes b(nullptr);
        ZenohCodec::serialize(std::make_pair<const uint8_t*, size_t>(s.data(), s.size()));
        return b;
    }

    template<class T>
    static Bytes serialize(const std::vector<T>& s) {
        return Bytes::serialize_from_iter(s.begin(), s.end());
    }

    template<class K, class V>
    static Bytes serialize(const std::unordered_map<K, V>& s) {
        return Bytes::serialize_from_iter(s.begin(), s.end());
    }

    template<class A, class B>
    static Bytes serialize(const std::pair<A, B>& s) {
        auto ba = ZenohCodec::serialize(s.first);
        auto bb = ZenohCodec::serialize(s.second);
        Bytes b(nullptr);
        ::z_bytes_encode_from_pair(detail::as_owned_c_ptr(b), ::z_move(*detail::as_owned_c_ptr(ba)), ::z_move(*detail::as_owned_c_ptr(bb)));
        return b;
    }

    template<class T>
    static T deserialize(const Bytes& b, ZError* err = nullptr) {
        return detail::ZenohDeserializer<T>::deserialize(b, err);
    }
};

}

