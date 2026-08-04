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
#include <utility>
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
    iterator(const Poco::JSON::Array::Ptr &arr, size_t index)
        : Storage(Poco::Dynamic::Var(arr)), state(index) {}

    // Object iterator constructor
    iterator(const Poco::JSON::Object::Ptr &obj,
             Poco::JSON::Object::ConstIterator it)
        : Storage(Poco::Dynamic::Var(obj)), state(it) {}

    // Common iterator operations
    auto operator++() -> iterator & {
        if (auto *arr = std::get_if<0>(&state)) {
            ++(*arr);
        } else if (auto *obj = std::get_if<1>(&state)) {
            ++(*obj);
        }
        return *this;
    }

    auto operator!=(const iterator &other) const -> bool {
        return state != other.state;
    }

    auto operator==(const iterator &other) const -> bool {
        return state == other.state;
    }

    auto operator*() const -> value_type {
        if (const auto *arr = std::get_if<0>(&state)) {
            return Storage.value.template extract<Poco::JSON::Array::Ptr>()
                ->get(*arr);
        }
        if (auto *obj = std::get_if<1>(&state)) {
            return (*obj)->second;
        }
        throw std::runtime_error("Invalid iterator state");
    }

    auto operator->() -> value_type * {
        CurrentStorage = **this;
        return &CurrentStorage;
    }

    // For object iteration: get current key
    auto key() const -> std::string {
        if (const auto *obj = std::get_if<1>(&state)) {
            return (*obj)->first;
        }
        throw std::runtime_error("Not an object iterator");
    }
};
} // namespace PocoJsonWrapperIt

struct PocoJsonWrapper {
    using iterator = PocoJsonWrapperIt::iterator<PocoJsonWrapper>;

    // Constructors for basic types
    PocoJsonWrapper() {} // Represents null

    template <class T> PocoJsonWrapper(T val) : value(val) {}

    PocoJsonWrapper(const char *val) : value(std::string(val)) {}
    PocoJsonWrapper(const std::string &val) : value(val) {}
    PocoJsonWrapper(std::nullptr_t) {}

    // Construct from Poco::Dynamic::Var
    PocoJsonWrapper(const Poco::Dynamic::Var &var) : value(var) {}

    PocoJsonWrapper(const PocoJsonWrapper &) = default;
    PocoJsonWrapper(PocoJsonWrapper &&) = default;

    auto operator=(const PocoJsonWrapper &) -> PocoJsonWrapper & = default;
    auto operator=(PocoJsonWrapper &&) -> PocoJsonWrapper & = default;

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
    static auto parse(const std::string &jsonStr) -> PocoJsonWrapper {
        Poco::JSON::Parser parser;
        Poco::Dynamic::Var result = parser.parse(jsonStr);
        return PocoJsonWrapper(result);
    }

    // Object element access
    class ObjectProxy {
        Poco::JSON::Object::Ptr obj_;
        std::string key_;

      public:
        ObjectProxy(Poco::JSON::Object::Ptr obj, std::string key)
            : obj_(std::move(std::move(obj))), key_(std::move(key)) {}

        auto operator=(const PocoJsonWrapper &val) -> ObjectProxy & {
            obj_->set(key_, val.value);
            return *this;
        }

        template <class T> auto get() const { return obj_->getValue<T>(key_); }

        operator PocoJsonWrapper() const {
            return PocoJsonWrapper(obj_->get(key_));
        }
    };

    auto operator[](const std::string &key) const -> ObjectProxy {
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
            : arr_(std::move(std::move(arr))), index_(index) {}

        auto operator=(const PocoJsonWrapper &val) -> ArrayProxy & {
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

    auto operator[](size_t index) const -> ArrayProxy const {
        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return {arr, index};
        }
        throw std::runtime_error("Not a JSON array");
    }

    void push_back(const PocoJsonWrapper &val) const {
        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            arr->add(val.value);
        } else {
            throw std::runtime_error("Not a JSON array");
        }
    }

    auto is_empty() const -> bool { return value.isEmpty(); }

    auto empty() const -> bool { return value.isEmpty(); }

    // Type checks
    auto is_object() const -> bool {
        return !value.isEmpty() &&
               value.type() == typeid(Poco::JSON::Object::Ptr);
    }

    auto is_array() const -> bool {
        return !value.isEmpty() &&
               value.type() == typeid(Poco::JSON::Array::Ptr);
    }

    auto is_string() const -> bool { return value.isString(); }
    auto is_number() const -> bool {
        return value.isInteger() || value.isNumeric();
    }
    auto is_boolean() const -> bool { return value.isBoolean(); }
    auto is_null() const -> bool { return value.isEmpty(); }

    // Size of object/array
    auto size() const -> size_t {
        if (is_object()) {
            return value.extract<Poco::JSON::Object::Ptr>()->size();
        }
        if (is_array()) {
            return value.extract<Poco::JSON::Array::Ptr>()->size();
        }
        return 0;
    }

    // Serialize to JSON string
    auto dump() const -> std::string {
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
            std::cerr << e.displayText() << '\n';
            throw;
        } catch (const std::exception &e) {
            std::cerr << e.what() << '\n';
            throw;
        }

        return oss.str();
    }

    // Value conversion
    template <typename T> auto get() const -> T { return value.convert<T>(); }

    auto begin() const -> iterator {
        if (empty()) {
            return iterator{};
        }

        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return {arr, 0};
        }
        if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            return iterator(obj, obj->begin());
        }

        return iterator{};
    }

    auto end() const -> iterator {
        if (empty()) {
            return iterator{};
        }

        if (is_array()) {
            auto arr = value.extract<Poco::JSON::Array::Ptr>();
            return {arr, arr->size()};
        }
        if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            return iterator(obj, obj->end());
        }

        return iterator{};
    }

    auto find(const std::string &key) const -> iterator {
        if (empty()) {
            return iterator{};
        }

        if (is_object()) {
            auto obj = value.extract<Poco::JSON::Object::Ptr>();
            auto it = std::find_if(obj->begin(), obj->end(), [&](auto &pair) {
                return pair.first == key;
            });
            if (it != obj->end()) {
                return {obj, it};
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
