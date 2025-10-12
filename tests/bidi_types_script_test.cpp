// tests/bidi_types_script_test.cpp - Unit tests for script types
#include "bidi/types/script.hpp"
#include <boost/json.hpp>
#include <gtest/gtest.h>
#include <variant>

using namespace bidi::types::script;

// ==================== RealmType Tests ====================

TEST(BidiTypesScript, RealmTypeToString) {
    using enum RealmType;

    EXPECT_EQ(to_string(Window), "window");
    EXPECT_EQ(to_string(DedicatedWorker), "dedicated-worker");
    EXPECT_EQ(to_string(SharedWorker), "shared-worker");
    EXPECT_EQ(to_string(ServiceWorker), "service-worker");
    EXPECT_EQ(to_string(Worker), "worker");
    EXPECT_EQ(to_string(PaintWorklet), "paint-worklet");
    EXPECT_EQ(to_string(AudioWorklet), "audio-worklet");
    EXPECT_EQ(to_string(Worklet), "worklet");
}

TEST(BidiTypesScript, RealmTypeConstexpr) {
    constexpr auto str = to_string(RealmType::Window);
    static_assert(str == "window");
}

TEST(BidiTypesScript, RealmTypeBoostJson) {
    using enum RealmType;

    auto jv1 = boost::json::value_from(Window);
    EXPECT_EQ(jv1.as_string(), "window");

    auto jv2 = boost::json::value_from(DedicatedWorker);
    EXPECT_EQ(jv2.as_string(), "dedicated-worker");

    auto jv3 = boost::json::value_from(ServiceWorker);
    EXPECT_EQ(jv3.as_string(), "service-worker");
}

// ==================== ResultOwnership Tests ====================

TEST(BidiTypesScript, ResultOwnershipToString) {
    using enum ResultOwnership;

    EXPECT_EQ(to_string(Root), "root");
    EXPECT_EQ(to_string(None), "none");
}

TEST(BidiTypesScript, ResultOwnershipConstexpr) {
    constexpr auto str = to_string(ResultOwnership::Root);
    static_assert(str == "root");
}

TEST(BidiTypesScript, ResultOwnershipBoostJson) {
    using enum ResultOwnership;

    auto jv1 = boost::json::value_from(Root);
    EXPECT_EQ(jv1.as_string(), "root");

    auto jv2 = boost::json::value_from(None);
    EXPECT_EQ(jv2.as_string(), "none");
}

// ==================== Remote Reference Tests ====================

TEST(BidiTypesScript, SharedReferenceEquality) {
    SharedReference ref1;
    ref1.shared_id = "shared-123";
    ref1.handle = "handle-456";

    SharedReference ref2;
    ref2.shared_id = "shared-123";
    ref2.handle = "handle-456";

    SharedReference ref3;
    ref3.shared_id = "shared-789";

    EXPECT_EQ(ref1, ref2);
    EXPECT_NE(ref1, ref3);
}

TEST(BidiTypesScript, SharedReferenceOptionalHandle) {
    SharedReference ref;
    ref.shared_id = "shared-123";

    // handle is optional
    EXPECT_FALSE(ref.handle.has_value());

    ref.handle = "handle-456";
    EXPECT_TRUE(ref.handle.has_value());
    EXPECT_EQ(*ref.handle, "handle-456");
}

TEST(BidiTypesScript, RemoteObjectReferenceEquality) {
    RemoteObjectReference ref1;
    ref1.handle = "handle-123";
    ref1.shared_id = "shared-456";

    RemoteObjectReference ref2;
    ref2.handle = "handle-123";
    ref2.shared_id = "shared-456";

    RemoteObjectReference ref3;
    ref3.handle = "handle-789";

    EXPECT_EQ(ref1, ref2);
    EXPECT_NE(ref1, ref3);
}

TEST(BidiTypesScript, RemoteObjectReferenceOptionalSharedId) {
    RemoteObjectReference ref;
    ref.handle = "handle-123";

    // shared_id is optional
    EXPECT_FALSE(ref.shared_id.has_value());

    ref.shared_id = "shared-456";
    EXPECT_TRUE(ref.shared_id.has_value());
    EXPECT_EQ(*ref.shared_id, "shared-456");
}

TEST(BidiTypesScript, RemoteReferenceVariantConstruction) {
    // Test construction with each reference type
    SharedReference shared_ref;
    shared_ref.shared_id = "shared-123";
    RemoteReference ref1 = shared_ref;
    EXPECT_TRUE(std::holds_alternative<SharedReference>(ref1));

    RemoteObjectReference obj_ref;
    obj_ref.handle = "handle-456";
    obj_ref.shared_id = "shared-789";
    RemoteReference ref2 = obj_ref;
    EXPECT_TRUE(std::holds_alternative<RemoteObjectReference>(ref2));
}

TEST(BidiTypesScript, RemoteReferenceVariantVisitor) {
    auto get_type = [](const RemoteReference &ref) -> std::string {
        return std::visit(
            [](const auto &r) -> std::string {
                using T = std::decay_t<decltype(r)>;
                if constexpr (std::is_same_v<T, SharedReference>) {
                    return "shared";
                } else if constexpr (std::is_same_v<T, RemoteObjectReference>) {
                    return "object";
                }
                return "unknown";
            },
            ref);
    };

    SharedReference sr;
    sr.shared_id = "s";
    EXPECT_EQ(get_type(sr), "shared");

    RemoteObjectReference ror;
    ror.handle = "h";
    EXPECT_EQ(get_type(ror), "object");
}

// ==================== RealmInfo Tests ====================

TEST(BidiTypesScript, RealmInfoEquality) {
    RealmInfo realm1;
    realm1.realm = "realm-1";
    realm1.type = RealmType::Window;
    realm1.origin = "https://example.com";
    realm1.agent_cluster_id = "cluster-1";

    RealmInfo realm2;
    realm2.realm = "realm-1";
    realm2.type = RealmType::Window;
    realm2.origin = "https://example.com";
    realm2.agent_cluster_id = "cluster-1";

    RealmInfo realm3;
    realm3.realm = "realm-2";
    realm3.type = RealmType::Worker;
    realm3.origin = "https://different.com";

    EXPECT_EQ(realm1, realm2);
    EXPECT_NE(realm1, realm3);
}

TEST(BidiTypesScript, RealmInfoOptionalFields) {
    RealmInfo realm;
    realm.realm = "realm-1";
    realm.type = RealmType::Window;
    realm.origin = "https://example.com";

    // agent_cluster_id is optional
    EXPECT_FALSE(realm.agent_cluster_id.has_value());

    realm.agent_cluster_id = "cluster-1";
    EXPECT_TRUE(realm.agent_cluster_id.has_value());
    EXPECT_EQ(*realm.agent_cluster_id, "cluster-1");
}

// ==================== Target Tests ====================

TEST(BidiTypesScript, TargetEquality) {
    Target target1;
    target1.context = "ctx-1";
    target1.sandbox = "sandbox-1";
    target1.realm = "realm-1";

    Target target2;
    target2.context = "ctx-1";
    target2.sandbox = "sandbox-1";
    target2.realm = "realm-1";

    Target target3;
    target3.context = "ctx-2";

    EXPECT_EQ(target1, target2);
    EXPECT_NE(target1, target3);
}

TEST(BidiTypesScript, TargetOptionalFields) {
    Target target;
    target.context = "ctx-1";

    // sandbox and realm are optional
    EXPECT_FALSE(target.sandbox.has_value());
    EXPECT_FALSE(target.realm.has_value());

    target.sandbox = "sandbox-1";
    target.realm = "realm-1";

    EXPECT_TRUE(target.sandbox.has_value());
    EXPECT_EQ(*target.sandbox, "sandbox-1");
    EXPECT_TRUE(target.realm.has_value());
    EXPECT_EQ(*target.realm, "realm-1");
}

// ==================== PrimitiveProtocolValue Tests ====================

TEST(BidiTypesScript, PrimitiveProtocolValueTypeEnum) {
    using Type = PrimitiveProtocolValue::Type;

    // Verify all enum values are distinct
    EXPECT_NE(Type::Undefined, Type::Null);
    EXPECT_NE(Type::String, Type::Number);
    EXPECT_NE(Type::Boolean, Type::BigInt);
}

TEST(BidiTypesScript, PrimitiveProtocolValueEquality) {
    PrimitiveProtocolValue val1;
    val1.type = PrimitiveProtocolValue::Type::String;
    val1.value = std::string("hello");

    PrimitiveProtocolValue val2;
    val2.type = PrimitiveProtocolValue::Type::String;
    val2.value = std::string("hello");

    PrimitiveProtocolValue val3;
    val3.type = PrimitiveProtocolValue::Type::Number;
    val3.value = 42.0;

    EXPECT_EQ(val1, val2);
    EXPECT_NE(val1, val3);
}

TEST(BidiTypesScript, PrimitiveProtocolValueSpecialNumbers) {
    PrimitiveProtocolValue nan_val;
    nan_val.type = PrimitiveProtocolValue::Type::Number;
    nan_val.special_number = "NaN";

    PrimitiveProtocolValue inf_val;
    inf_val.type = PrimitiveProtocolValue::Type::Number;
    inf_val.special_number = "Infinity";

    PrimitiveProtocolValue neg_zero;
    neg_zero.type = PrimitiveProtocolValue::Type::Number;
    neg_zero.special_number = "-0";

    EXPECT_EQ(*nan_val.special_number, "NaN");
    EXPECT_EQ(*inf_val.special_number, "Infinity");
    EXPECT_EQ(*neg_zero.special_number, "-0");
}

TEST(BidiTypesScript, PrimitiveProtocolValueVariantAccess) {
    PrimitiveProtocolValue str_val;
    str_val.type = PrimitiveProtocolValue::Type::String;
    str_val.value = std::string("test");

    // Access string value
    EXPECT_TRUE(std::holds_alternative<std::string>(str_val.value));
    EXPECT_EQ(std::get<std::string>(str_val.value), "test");

    PrimitiveProtocolValue num_val;
    num_val.type = PrimitiveProtocolValue::Type::Number;
    num_val.value = 123.45;

    // Access number value
    EXPECT_TRUE(std::holds_alternative<double>(num_val.value));
    EXPECT_DOUBLE_EQ(std::get<double>(num_val.value), 123.45);

    PrimitiveProtocolValue bool_val;
    bool_val.type = PrimitiveProtocolValue::Type::Boolean;
    bool_val.value = true;

    // Access boolean value
    EXPECT_TRUE(std::holds_alternative<bool>(bool_val.value));
    EXPECT_TRUE(std::get<bool>(bool_val.value));
}

// ==================== SymbolRemoteValue Tests ====================

TEST(BidiTypesScript, SymbolRemoteValueEquality) {
    SymbolRemoteValue sym1;
    sym1.handle = "handle-123";
    sym1.internal_id = "internal-456";

    SymbolRemoteValue sym2;
    sym2.handle = "handle-123";
    sym2.internal_id = "internal-456";

    SymbolRemoteValue sym3;
    sym3.handle = "handle-789";

    EXPECT_EQ(sym1, sym2);
    EXPECT_NE(sym1, sym3);
}

TEST(BidiTypesScript, SymbolRemoteValueOptionalFields) {
    SymbolRemoteValue sym;

    EXPECT_FALSE(sym.handle.has_value());
    EXPECT_FALSE(sym.internal_id.has_value());

    sym.handle = "handle-123";
    sym.internal_id = "internal-456";

    EXPECT_TRUE(sym.handle.has_value());
    EXPECT_TRUE(sym.internal_id.has_value());
}

// ==================== ArrayRemoteValue Tests ====================

TEST(BidiTypesScript, ArrayRemoteValueEquality) {
    ArrayRemoteValue arr1;
    arr1.handle = "handle-123";
    arr1.internal_id = "internal-456";

    ArrayRemoteValue arr2;
    arr2.handle = "handle-123";
    arr2.internal_id = "internal-456";

    ArrayRemoteValue arr3;
    arr3.handle = "handle-789";

    EXPECT_EQ(arr1, arr2);
    EXPECT_NE(arr1, arr3);
}

TEST(BidiTypesScript, ArrayRemoteValueWithElements) {
    ArrayRemoteValue arr;
    arr.handle = "handle-123";

    std::vector<boost::json::value> elements;
    elements.push_back(boost::json::value(1));
    elements.push_back(boost::json::value("two"));
    elements.push_back(boost::json::value(true));
    arr.value = elements;

    EXPECT_TRUE(arr.value.has_value());
    EXPECT_EQ(arr.value->size(), 3U);
}

// ==================== ObjectRemoteValue Tests ====================

TEST(BidiTypesScript, ObjectPropertyEquality) {
    ObjectProperty prop1;
    prop1.name = "foo";
    prop1.value = boost::json::value(42);

    ObjectProperty prop2;
    prop2.name = "foo";
    prop2.value = boost::json::value(42);

    ObjectProperty prop3;
    prop3.name = "bar";
    prop3.value = boost::json::value("baz");

    EXPECT_EQ(prop1, prop2);
    EXPECT_NE(prop1, prop3);
}

TEST(BidiTypesScript, ObjectRemoteValueEquality) {
    ObjectRemoteValue obj1;
    obj1.handle = "handle-123";

    ObjectRemoteValue obj2;
    obj2.handle = "handle-123";

    ObjectRemoteValue obj3;
    obj3.handle = "handle-456";

    EXPECT_EQ(obj1, obj2);
    EXPECT_NE(obj1, obj3);
}

TEST(BidiTypesScript, ObjectRemoteValueWithProperties) {
    ObjectRemoteValue obj;
    obj.handle = "handle-123";

    std::vector<ObjectProperty> props;
    props.push_back(
        ObjectProperty{.name = "x", .value = boost::json::value(10)});
    props.push_back(
        ObjectProperty{.name = "y", .value = boost::json::value(20)});
    obj.value = props;

    EXPECT_TRUE(obj.value.has_value());
    EXPECT_EQ(obj.value->size(), 2U);
    EXPECT_EQ((*obj.value)[0].name, "x");
    EXPECT_EQ((*obj.value)[1].name, "y");
}

// ==================== FunctionRemoteValue Tests ====================

TEST(BidiTypesScript, FunctionRemoteValueEquality) {
    FunctionRemoteValue fn1;
    fn1.handle = "handle-123";
    fn1.internal_id = "internal-456";

    FunctionRemoteValue fn2;
    fn2.handle = "handle-123";
    fn2.internal_id = "internal-456";

    FunctionRemoteValue fn3;
    fn3.handle = "handle-789";

    EXPECT_EQ(fn1, fn2);
    EXPECT_NE(fn1, fn3);
}

// ==================== RegExpRemoteValue Tests ====================

TEST(BidiTypesScript, RegExpRemoteValueEquality) {
    RegExpRemoteValue re1;
    re1.handle = "handle-123";
    re1.pattern = "[a-z]+";
    re1.flags = "gi";

    RegExpRemoteValue re2;
    re2.handle = "handle-123";
    re2.pattern = "[a-z]+";
    re2.flags = "gi";

    RegExpRemoteValue re3;
    re3.pattern = "[0-9]+";

    EXPECT_EQ(re1, re2);
    EXPECT_NE(re1, re3);
}

TEST(BidiTypesScript, RegExpRemoteValueOptionalFlags) {
    RegExpRemoteValue re;
    re.pattern = "[a-z]+";

    EXPECT_FALSE(re.flags.has_value());

    re.flags = "gi";
    EXPECT_TRUE(re.flags.has_value());
    EXPECT_EQ(*re.flags, "gi");
}

// ==================== DateRemoteValue Tests ====================

TEST(BidiTypesScript, DateRemoteValueEquality) {
    DateRemoteValue date1;
    date1.handle = "handle-123";
    date1.value = "2024-01-01T00:00:00.000Z";

    DateRemoteValue date2;
    date2.handle = "handle-123";
    date2.value = "2024-01-01T00:00:00.000Z";

    DateRemoteValue date3;
    date3.value = "2025-01-01T00:00:00.000Z";

    EXPECT_EQ(date1, date2);
    EXPECT_NE(date1, date3);
}

// ==================== MapRemoteValue Tests ====================

TEST(BidiTypesScript, MapRemoteValueEquality) {
    MapRemoteValue map1;
    map1.handle = "handle-123";

    MapRemoteValue map2;
    map2.handle = "handle-123";

    MapRemoteValue map3;
    map3.handle = "handle-456";

    EXPECT_EQ(map1, map2);
    EXPECT_NE(map1, map3);
}

// ==================== SetRemoteValue Tests ====================

TEST(BidiTypesScript, SetRemoteValueEquality) {
    SetRemoteValue set1;
    set1.handle = "handle-123";

    SetRemoteValue set2;
    set2.handle = "handle-123";

    SetRemoteValue set3;
    set3.handle = "handle-456";

    EXPECT_EQ(set1, set2);
    EXPECT_NE(set1, set3);
}

// ==================== NodeRemoteValue Tests ====================

TEST(BidiTypesScript, NodeRemoteValueEquality) {
    NodeRemoteValue node1;
    node1.handle = "handle-123";
    node1.shared_id = "shared-456";
    node1.node_type = "Element";
    node1.local_name = "div";

    NodeRemoteValue node2;
    node2.handle = "handle-123";
    node2.shared_id = "shared-456";
    node2.node_type = "Element";
    node2.local_name = "div";

    NodeRemoteValue node3;
    node3.handle = "handle-789";
    node3.local_name = "span";

    EXPECT_EQ(node1, node2);
    EXPECT_NE(node1, node3);
}

TEST(BidiTypesScript, NodeRemoteValueOptionalFields) {
    NodeRemoteValue node;
    node.handle = "handle-123";

    EXPECT_FALSE(node.shared_id.has_value());
    EXPECT_FALSE(node.node_type.has_value());
    EXPECT_FALSE(node.local_name.has_value());

    node.shared_id = "shared-456";
    node.node_type = "Element";
    node.local_name = "div";

    EXPECT_TRUE(node.shared_id.has_value());
    EXPECT_TRUE(node.node_type.has_value());
    EXPECT_TRUE(node.local_name.has_value());
}

// ==================== RemoteValue Variant Tests ====================

TEST(BidiTypesScript, RemoteValueVariantConstruction) {
    // Test construction with each RemoteValue type
    RemoteValue val1 = PrimitiveProtocolValue{};
    EXPECT_TRUE(std::holds_alternative<PrimitiveProtocolValue>(val1));

    RemoteValue val2 = SymbolRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<SymbolRemoteValue>(val2));

    RemoteValue val3 = ArrayRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<ArrayRemoteValue>(val3));

    RemoteValue val4 = ObjectRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<ObjectRemoteValue>(val4));

    RemoteValue val5 = FunctionRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<FunctionRemoteValue>(val5));

    RegExpRemoteValue regexp;
    regexp.pattern = "[a-z]+";
    RemoteValue val6 = regexp;
    EXPECT_TRUE(std::holds_alternative<RegExpRemoteValue>(val6));

    DateRemoteValue date;
    date.value = "2024-01-01T00:00:00.000Z";
    RemoteValue val7 = date;
    EXPECT_TRUE(std::holds_alternative<DateRemoteValue>(val7));

    RemoteValue val8 = MapRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<MapRemoteValue>(val8));

    RemoteValue val9 = SetRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<SetRemoteValue>(val9));

    RemoteValue val10 = NodeRemoteValue{};
    EXPECT_TRUE(std::holds_alternative<NodeRemoteValue>(val10));
}

TEST(BidiTypesScript, RemoteValueVariantVisitor) {
    auto get_type_name = [](const RemoteValue &val) -> std::string {
        return std::visit(
            [](const auto &v) -> std::string {
                using T = std::decay_t<decltype(v)>;
                if constexpr (std::is_same_v<T, PrimitiveProtocolValue>) {
                    return "primitive";
                } else if constexpr (std::is_same_v<T, SymbolRemoteValue>) {
                    return "symbol";
                } else if constexpr (std::is_same_v<T, ArrayRemoteValue>) {
                    return "array";
                } else if constexpr (std::is_same_v<T, ObjectRemoteValue>) {
                    return "object";
                } else if constexpr (std::is_same_v<T, FunctionRemoteValue>) {
                    return "function";
                } else if constexpr (std::is_same_v<T, RegExpRemoteValue>) {
                    return "regexp";
                } else if constexpr (std::is_same_v<T, DateRemoteValue>) {
                    return "date";
                } else if constexpr (std::is_same_v<T, MapRemoteValue>) {
                    return "map";
                } else if constexpr (std::is_same_v<T, SetRemoteValue>) {
                    return "set";
                } else if constexpr (std::is_same_v<T, NodeRemoteValue>) {
                    return "node";
                }
                return "unknown";
            },
            val);
    };

    EXPECT_EQ(get_type_name(PrimitiveProtocolValue{}), "primitive");
    EXPECT_EQ(get_type_name(SymbolRemoteValue{}), "symbol");
    EXPECT_EQ(get_type_name(ArrayRemoteValue{}), "array");
    EXPECT_EQ(get_type_name(ObjectRemoteValue{}), "object");
    EXPECT_EQ(get_type_name(FunctionRemoteValue{}), "function");

    RegExpRemoteValue re;
    re.pattern = "x";
    EXPECT_EQ(get_type_name(re), "regexp");

    DateRemoteValue dt;
    dt.value = "2024";
    EXPECT_EQ(get_type_name(dt), "date");

    EXPECT_EQ(get_type_name(MapRemoteValue{}), "map");
    EXPECT_EQ(get_type_name(SetRemoteValue{}), "set");
    EXPECT_EQ(get_type_name(NodeRemoteValue{}), "node");
}

TEST(BidiTypesScript, RemoteValueVariantIndexTest) {
    // Verify variant indices are correct
    RemoteValue val0 = PrimitiveProtocolValue{};
    EXPECT_EQ(val0.index(), 0U);

    RemoteValue val1 = SymbolRemoteValue{};
    EXPECT_EQ(val1.index(), 1U);

    RemoteValue val2 = ArrayRemoteValue{};
    EXPECT_EQ(val2.index(), 2U);

    RemoteValue val3 = ObjectRemoteValue{};
    EXPECT_EQ(val3.index(), 3U);

    RemoteValue val4 = FunctionRemoteValue{};
    EXPECT_EQ(val4.index(), 4U);

    RegExpRemoteValue re_empty;
    re_empty.pattern = "";
    RemoteValue val5 = re_empty;
    EXPECT_EQ(val5.index(), 5U);

    DateRemoteValue dt_empty;
    dt_empty.value = "";
    RemoteValue val6 = dt_empty;
    EXPECT_EQ(val6.index(), 6U);

    RemoteValue val7 = MapRemoteValue{};
    EXPECT_EQ(val7.index(), 7U);

    RemoteValue val8 = SetRemoteValue{};
    EXPECT_EQ(val8.index(), 8U);

    RemoteValue val9 = NodeRemoteValue{};
    EXPECT_EQ(val9.index(), 9U);
}

// ==================== LocalValue Tests ====================

TEST(BidiTypesScript, LocalValueEquality) {
    LocalValue local1;
    local1.type = "string";
    local1.value = boost::json::value("hello");

    LocalValue local2;
    local2.type = "string";
    local2.value = boost::json::value("hello");

    LocalValue local3;
    local3.type = "number";
    local3.value = boost::json::value(42);

    EXPECT_EQ(local1, local2);
    EXPECT_NE(local1, local3);
}

TEST(BidiTypesScript, LocalValueOptionalValue) {
    LocalValue local;
    local.type = "undefined";

    EXPECT_FALSE(local.value.has_value());

    local.value = boost::json::value(123);
    EXPECT_TRUE(local.value.has_value());
}

TEST(BidiTypesScript, LocalValueDefaultConstruction) {
    LocalValue local;

    EXPECT_TRUE(local.type.empty());
    EXPECT_FALSE(local.value.has_value());
}

// ==================== Type Safety Tests ====================

TEST(BidiTypesScript, StructDefaultConstruction) {
    // Verify all structs are default-constructible
    SharedReference shared_ref;
    EXPECT_TRUE(shared_ref.shared_id.empty());
    EXPECT_FALSE(shared_ref.handle.has_value());

    RemoteObjectReference obj_ref;
    EXPECT_TRUE(obj_ref.handle.empty());
    EXPECT_FALSE(obj_ref.shared_id.has_value());

    RealmInfo realm;
    EXPECT_TRUE(realm.realm.empty());
    EXPECT_FALSE(realm.agent_cluster_id.has_value());

    Target target;
    EXPECT_TRUE(target.context.empty());
    EXPECT_FALSE(target.sandbox.has_value());

    PrimitiveProtocolValue prim;
    EXPECT_FALSE(prim.special_number.has_value());

    SymbolRemoteValue sym;
    EXPECT_FALSE(sym.handle.has_value());

    ArrayRemoteValue arr;
    EXPECT_FALSE(arr.value.has_value());

    ObjectRemoteValue obj;
    EXPECT_FALSE(obj.value.has_value());

    FunctionRemoteValue fn;
    EXPECT_FALSE(fn.handle.has_value());

    RegExpRemoteValue re;
    EXPECT_TRUE(re.pattern.empty());

    DateRemoteValue date;
    EXPECT_TRUE(date.value.empty());

    MapRemoteValue map;
    EXPECT_FALSE(map.handle.has_value());

    SetRemoteValue set;
    EXPECT_FALSE(set.handle.has_value());

    NodeRemoteValue node;
    EXPECT_FALSE(node.shared_id.has_value());

    LocalValue local;
    EXPECT_TRUE(local.type.empty());
}
