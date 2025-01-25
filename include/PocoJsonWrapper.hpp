#pragma once

#include <Poco/Dynamic/Var.h>
#include <Poco/Exception.h>
#include <Poco/JSON/Array.h>
#include <Poco/JSON/Object.h>
#include <Poco/JSON/Parser.h>
#include <Poco/JSON/Stringifier.h>
#include <algorithm>
#include <sstream>
#include <stdexcept>
#include <variant>

namespace PocoJsonWrapperIt {

// Unified iterator implementation
template <class T> struct iterator {
    using iterator_category = std::forward_iterator_tag;
    using value_type = T;
    using difference_type = std::ptrdiff_t;
    using pointer = value_type *;
    using reference = value_type &;

    value_type Storage;
    mutable value_type CurrentStorage;

    // State variants for array/object iteration
    std::variant<size_t, Poco::JSON::Object::ConstIterator> state;

    iterator() = default;

    // Array iterator constructor
    iterator(Poco::JSON::Array::Ptr arr, size_t index)
        : Storage(Poco::Dynamic::Var(arr)), state(index) {}

    // Object iterator constructor
    iterator(Poco::JSON::Object::Ptr obj, Poco::JSON::Object::ConstIterator it)
        : Storage(Poco::Dynamic::Var(obj)), state(it) {}

    // Common iterator operations
    iterator &operator++() {
        if (auto *arr = std::get_if<0>(&state)) {
            ++(*arr);
        } else if (auto *obj = std::get_if<1>(&state)) {
            ++(*obj);
        }
        return *this;
    }

    bool operator!=(const iterator &other) const {
        return state != other.state;
    }

    bool operator==(const iterator &other) const {
        return state == other.state;
    }

    value_type operator*() const {
        if (auto *arr = std::get_if<0>(&state)) {
            return Storage.value.template extract<Poco::JSON::Array::Ptr>()
                ->get(*arr);
        } else if (auto *obj = std::get_if<1>(&state)) {
            return (*obj)->second;
        }
        throw std::runtime_error("Invalid iterator state");
    }

    value_type *operator->() {
        CurrentStorage = **this;
        return &CurrentStorage;
    }

    // For object iteration: get current key
    std::string key() const {
        if (auto *obj = std::get_if<1>(&state)) {
            return (*obj)->first;
        }
        throw std::runtime_error("Not an object iterator");
    }
};
} // namespace PocoJsonWrapperIt

struct PocoJsonWrapper {
    using iterator = PocoJsonWrapperIt::iterator<PocoJsonWrapper>;

    // Constructors for basic types
    PocoJsonWrapper() : value(Poco::Dynamic::Var()) {} // Represents null

    template <class T> PocoJsonWrapper(T val) : value(val) {}

    PocoJsonWrapper(const char *val) : value(std::string(val)) {}
    PocoJsonWrapper(const std::string &val) : value(val) {}
    PocoJsonWrapper(std::nullptr_t) : value(Poco::Dynamic::Var()) {}

    // Construct from Poco::Dynamic::Var
    PocoJsonWrapper(const Poco::Dynamic::Var &var) : value(var) {}

    PocoJsonWrapper(const PocoJsonWrapper &) = default;
    PocoJsonWrapper(PocoJsonWrapper &&) = default;

    PocoJsonWrapper &operator=(const PocoJsonWrapper &) = default;
    PocoJsonWrapper &operator=(PocoJsonWrapper &&) = default;

    // Construct objects from key-value pairs
    PocoJsonWrapper(
        std::initializer_list<std::pair<const std::string, PocoJsonWrapper>>
            init) {
        Poco::JSON::Object::Ptr obj(new Poco::JSON::Object);
        for (const auto &pair : init) {
            obj->set(pair.first, pair.second.value);
        }
        value = obj;
    }

    // Construct arrays from elements
    PocoJsonWrapper(std::initializer_list<PocoJsonWrapper> init) {
        Poco::JSON::Array::Ptr arr(new Poco::JSON::Array);
        for (const auto &elem : init) {
            arr->add(elem.value);
        }
        value = arr;
    }

    // Parse JSON string
    static PocoJsonWrapper parse(const std::string &jsonStr) {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(jsonStr);
        return PocoJsonWrapper(result);
    }

    // Object element access
    class ObjectProxy {
        Poco::JSON::Object::Ptr obj_;
        std::string key_;

      public:
        ObjectProxy(Poco::JSON::Object::Ptr obj, const std::string &key)
            : obj_(obj), key_(key) {}

        ObjectProxy &operator=(const PocoJsonWrapper &val) {
            obj_->set(key_, val.value);
            return *this;
        }

        template <class T> auto get() const { return obj_->getValue<T>(key_); }

        operator PocoJsonWrapper() const {
            return PocoJsonWrapper(obj_->get(key_));
        }
    };

    ObjectProxy operator[](const std::string &key) const {
        if (is_empty()) {
            Poco::JSON::Object::Ptr obj = new Poco::JSON::Object;
            value = obj;

            return ObjectProxy(obj, key);
        }

        if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            return ObjectProxy(obj, key);
        }
        throw std::runtime_error("Not a JSON object");
    }

    // Array element access
    class ArrayProxy {
        Poco::JSON::Array::Ptr arr_;
        size_t index_;

      public:
        ArrayProxy(Poco::JSON::Array::Ptr arr, size_t index)
            : arr_(arr), index_(index) {}

        ArrayProxy &operator=(const PocoJsonWrapper &val) {
            if (index_ >= arr_->size()) {
                arr_->add(val.value);
            } else {
                arr_->set(static_cast<unsigned int>(index_), val.value);
            }
            return *this;
        }

        operator PocoJsonWrapper() const {
            return PocoJsonWrapper(
                arr_->get(static_cast<unsigned int>(index_)));
        }
    };

    ArrayProxy operator[](size_t index) {
        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return ArrayProxy(arr, index);
        }
        throw std::runtime_error("Not a JSON array");
    }

    void push_back(const PocoJsonWrapper &val) {
        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            arr->add(val.value);
        } else {
            throw std::runtime_error("Not a JSON array");
        }
    }

    bool is_empty() const { return value.isEmpty(); }

    bool empty() const { return value.isEmpty(); }

    // Type checks
    bool is_object() const {
        return !value.isEmpty() &&
               value.type() == typeid(Poco::JSON::Object::Ptr);
    }

    bool is_array() const {
        return !value.isEmpty() &&
               value.type() == typeid(Poco::JSON::Array::Ptr);
    }

    bool is_string() const { return value.isString(); }
    bool is_number() const { return value.isInteger() || value.isNumeric(); }
    bool is_boolean() const { return value.isBoolean(); }
    bool is_null() const { return value.isEmpty(); }

    // Size of object/array
    size_t size() const {
        if (is_object()) {
            return value.extract<Poco::JSON::Object::Ptr>()->size();
        } else if (is_array()) {
            return value.extract<Poco::JSON::Array::Ptr>()->size();
        }
        return 0;
    }

    // Serialize to JSON string
    std::string dump() const {
        std::ostringstream oss;
        try {
            if (is_object()) {
                value.extract<Poco::JSON::Object::Ptr>()->stringify(oss);
            } else if (is_array()) {
                value.extract<Poco::JSON::Array::Ptr>()->stringify(oss);
            } else {
                throw std::runtime_error("Not a JSON object or array");
            }
        } catch (const Poco::Exception &e) {
            std::cerr << e.displayText() << std::endl;
            throw;
        } catch (const std::exception &e) {
            std::cerr << e.what() << std::endl;
            throw;
        }

        return oss.str();
    }

    // Value conversion
    template <typename T> T get() const { return value.convert<T>(); }

    iterator begin() const {
        if (empty()) {
            return iterator{};
        }

        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return iterator(arr, 0);
        } else if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            return iterator(obj, obj->begin());
        }

        return iterator{};
    }

    iterator end() const {
        if (empty()) {
            return iterator{};
        }

        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return iterator(arr, arr->size());
        } else if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            return iterator(obj, obj->end());
        }

        return iterator{};
    }

    iterator find(const std::string &key) const {
        if (empty()) {
            return iterator{};
        }

        if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            auto it = std::find_if(obj->begin(), obj->end(), [&](auto &pair) {
                return pair.first == key;
            });
            if (it != obj->end()) {
                return iterator(obj, it);
            }
        }

        return end();
    }

    static auto
    object(std::initializer_list<std::pair<std::string, Poco::Dynamic::Var>>
               args = {}) {
        Poco::JSON::Object::Ptr obj(new Poco::JSON::Object);

        for (const auto &[key, value] : args) {
            if (value.type() == typeid(PocoJsonWrapper)) {
                auto wrapper = value.extract<PocoJsonWrapper>();
                while (!wrapper.empty() &&
                       wrapper.value.type() == typeid(PocoJsonWrapper)) {
                    wrapper = wrapper.value.extract<PocoJsonWrapper>();
                }

                obj->set(key, wrapper.value);
            } else {
                obj->set(key, value);
            }
        }

        return PocoJsonWrapper(Poco::Dynamic::Var(obj));
    }

    static auto array(std::initializer_list<Poco::Dynamic::Var> args = {}) {
        Poco::JSON::Array::Ptr arr(new Poco::JSON::Array);

        for (const auto &arg : args) {
            if (arg.type() == typeid(PocoJsonWrapper)) {
                auto wrapper = arg.extract<PocoJsonWrapper>();
                while (!wrapper.empty() &&
                       wrapper.value.type() == typeid(PocoJsonWrapper)) {
                    wrapper = wrapper.value.extract<PocoJsonWrapper>();
                }

                arr->add(wrapper.value);
            } else {
                arr->add(arg);
            }
        }

        return PocoJsonWrapper(Poco::Dynamic::Var(arr));
    }

    mutable Poco::Dynamic::Var value;
};
