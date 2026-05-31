#pragma once

#include <functional>
#include <map>
#include <memory>
#include <quickjs.h>
#include <stdexcept>
#include <tuple>
#include <unordered_map>
#include <utility>
#include <vector>

#ifdef QUICKJS_USE_QT
#include <QString>
#include <QVariant>
#include <QVariantList>
#include <QVariantMap>
#endif

namespace qjs {

    // Generates a lambda to GET a field value
    template <typename ClassType, typename FieldType>
    static auto make_getter(FieldType ClassType::*field_ptr) {
        return [field_ptr](ClassType* instance) -> FieldType {
            return instance->*field_ptr;
        };
    }

    // Generates a lambda to SET a field value
    template <typename ClassType, typename FieldType>
    static auto make_setter(FieldType ClassType::*field_ptr) {
        return [field_ptr](ClassType* instance, FieldType value) -> FieldType {
            return instance->*field_ptr = value;
        };
    }

class Value;

#ifdef QUICKJS_USE_QT
template <typename T>
constexpr bool is_qtstring_or_derived = std::is_base_of_v<QString, T>;
#endif

class Context;

/**
 * @brief Abstract interface for objects that can provide a JS context.
 *
 * This class is used as a base for objects that need access
 * to a QuickJS execution context.
 */
class ContextGetter {
public:
  /**
   * @brief Returns a Context object representing the JS execution context.
   *
   * @return A Context instance associated with this object.
   */
  virtual Context getContext() const;

protected:
  /**
   * @brief Returns the raw QuickJS context pointer.
   *
   * Derived classes must implement this to provide access
   * to the underlying JSContext.
   *
   * @return Pointer to the JSContext.
   */
  virtual JSContext *context() const = 0;
};

/**
 * @brief Abstract interface representing a JavaScript-like array.
 *
 * Provides indexed access to elements, allowing retrieval, assignment,
 * and querying of the array size. Works in conjunction with a JS context.
 */
class Array : public ContextGetter {
public:
  /**
   * @brief Retrieves the value at a given index.
   *
   * @param index The position of the element to retrieve
   * @return The Value at the specified index, or undefined if out-of-bounds
   */
  virtual Value get(size_t index) const = 0;

  /**
   * @brief Sets the value at a specific index.
   *
   * @param index The position to set
   * @param value The Value to assign
   * @return true if the assignment was successful, false otherwise
   */
  virtual bool set(size_t index, Value value) = 0;

  /**
   * @brief Returns the number of elements in the array.
   *
   * @return The size of the array
   */
  virtual size_t size() const = 0;

  /**
   * @brief Template convenience function to assign any C++ type at an index.
   *
   * Converts the provided value to a qjs::Value using the current context.
   *
   * @tparam T Any type convertible to qjs::Value
   * @param index The array index
   * @param value The value to assign
   * @return true if the assignment succeeded
   */
  template <typename T> bool set(size_t index, T value);

  /// @brief Virtual destructor for safe polymorphic destruction
  virtual ~Array() = default;
};

/**
 * @brief Abstract interface for JavaScript-like object (key-value map) access.
 *
 * This class represents an object that exposes string-keyed properties,
 * similar to a JavaScript object. It provides basic operations for:
 * - Retrieving properties by name
 * - Setting properties
 * - Removing properties
 * - Querying the number of properties
 *
 * Derived classes are expected to implement storage and lifecycle semantics.
 */

class Map : public ContextGetter {
public:
  /**
   * @brief Retrieves a value by property name.
   *
   * @param name The property key to look up
   * @return The value associated with the key, or JS undefined if not found
   */
  virtual qjs::Value get(const std::string &name) const = 0;

#ifdef QUICKJS_USE_QT
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  qjs::Value get(T name) const;
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool set(T name, qjs::Value value);
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool drop(T name);
  template <typename V, typename T,
            std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool set(T index, V value);
#endif
  /**
   * @brief Sets a property value by name.
   *
   * @param index The property key to set
   * @param value The value to assign
   * @return true if the operation succeeded, false otherwise
   */
  virtual bool set(const std::string &index, qjs::Value value) = 0;

  /**
   * @brief Removes a property from the object.
   *
   * @param index The property key to remove
   * @return true if the property was removed, false if it did not exist or
   * could not be removed
   */
  virtual bool drop(const std::string &index) = 0;

  /**
   * @brief Returns the number of properties in the map.
   *
   * @return The number of enumerable or stored properties
   */
  virtual size_t size() const = 0;

  /**
   * @brief Convenience setter for arbitrary C++ value types.
   *
   * Converts the given value into a qjs::Value using the current context
   * and stores it under the specified key.
   *
   * @tparam T Any type convertible to qjs::Value
   * @param index The property key
   * @param value The value to assign
   * @return true if the assignment succeeded
   */
  template <typename T> bool set(const std::string &index, T value);

  /// @brief Virtual destructor for safe polymorphic destruction
  virtual ~Map() = default;
};

template <typename T>
constexpr bool is_array_or_derived = std::is_base_of_v<Array, T>;

template <typename T>
constexpr bool is_not_jsvalue =
    !std::is_base_of_v<Value, std::remove_pointer_t<std::decay_t<T>>> &&
    !std::is_base_of_v<JSValue, std::remove_pointer_t<std::decay_t<T>>>;

template <typename T>
using is_not_array = std::integral_constant<bool, !is_array_or_derived<T>>;

template <typename... Ts> struct all_not_array;

template <> struct all_not_array<> : std::true_type {};

template <typename T, typename... Rest>
struct all_not_array<T, Rest...>
    : std::conditional_t<is_not_array<std::decay_t<T>>::value,
                         all_not_array<Rest...>, std::false_type> {};

// 1. Primary template declaration
template <typename T> struct callable_traits;

// 2. Primary template definition (inherits from the operator() member pointer)
template <typename T>
struct callable_traits : callable_traits<decltype(&T::operator())> {};

// 3. Specialization for non-const member function pointers
template <typename Ret, typename Base, typename... Args>
struct callable_traits<Ret (Base::*)(Args...)> {
  using return_type = Ret;
  using args_tuple = std::tuple<Args...>;
  using base_type = Base;
  static constexpr size_t args_count = sizeof...(Args);
};

// 4. Specialization for const member function pointers (lambdas match here)
template <typename Ret, typename Base, typename... Args>
struct callable_traits<Ret (Base::*)(Args...) const> {
  using return_type = Ret;
  using args_tuple = std::tuple<Args...>;
  static constexpr size_t args_count = sizeof...(Args);
};

/**
 * @brief Abstract representation of a JavaScript callable function.
 *
 * This class models a JS function object that can be invoked from C++.
 * It provides a unified interface for calling functions with different
 * argument styles and supports integration with QuickJS.
 *
 * Derived classes must implement the `call()` method.
 */
class Function : public ContextGetter {
public:
  /**
   * @brief Invokes the function with an explicit this-value and argument array.
   *
   * @tparam Array A type derived from qjs::Array
   * @param th The JavaScript `this` value
   * @param xs Argument array
   * @return Result of function execution
   */

  template <typename Array,
            std::enable_if_t<(is_array_or_derived<Array>), int> = 0>
  qjs::Value invoke(qjs::Value *th, Array *xs);

  /**
   * @brief Invokes the function with no arguments and default this-value.
   *
   * @return Result of function execution
   */
  qjs::Value invoke();

  /**
   * @brief Invokes the function with variadic arguments and explicit
   * this-value.
   *
   * Arguments are automatically packed into a temporary Array implementation.
   *
   * @tparam T Argument types (must not be Array types)
   * @param th The JavaScript `this` value
   * @param xs Variadic arguments to pass to the function
   * @return Result of function execution
   */
  template <typename... T,
            std::enable_if_t<all_not_array<T...>::value, int> = 0>
  qjs::Value invoke(qjs::Value *th, T &&...xs);

  /// @brief Virtual destructor for safe polymorphic destruction
  virtual ~Function() = default;

protected:
  /**
   * @brief Executes the underlying function implementation.
   *
   * @param this_val The JavaScript `this` value
   * @param p Argument array (may be null)
   * @return Result of execution as a qjs::Value
   */
  virtual qjs::Value call(qjs::Value *, qjs::Array *p = nullptr) = 0;
};

/**
 * Create new qjs::Value from function pointer
 */
static Value newFunctionValue(JSContext *ctx, qjs::Function *func);

/**
 * Create new qjs::Value from function
 */
static Value newFunctionValue(JSContext *ctx,
                              std::function<Value(Value *, Array *)> func);

// 1. Helper to safely detect if a class has an operator() (lambdas/functors)
template <typename T, typename = void> struct is_functor : std::false_type {};

template <typename T>
struct is_functor<T, std::void_t<decltype(&T::operator())>> : std::true_type {};

// 2. Helper to verify if a raw pointer is specifically a function pointer
template <typename T>
constexpr bool is_raw_function_pointer =
    std::is_pointer_v<T> && std::is_function_v<std::remove_pointer_t<T>>;

// 3. Your updated template variable constraint
template <typename T>
constexpr bool is_template_function =
    // A. Prevent infinite recursion overloads
    !std::is_convertible_v<T, std::function<Value(Value *, Array *)>> &&

    // B. Exclude internal qjs classes
    !std::is_base_of_v<qjs::Function, std::remove_pointer_t<std::decay_t<T>>> &&

    // C. ONLY allow lambdas/functors OR raw function pointers OR member
    // function pointers
    (is_functor<std::decay_t<T>>::value ||
     is_raw_function_pointer<std::decay_t<T>> ||
     std::is_member_function_pointer_v<std::decay_t<T>>);

template <typename T, std::enable_if_t<is_template_function<T>, int> = 0>
Value newFunctionValue(JSContext *ctx, T fn);

/**
 * @brief Concrete Function implementation backed by a std::function callback.
 *
 * This class allows C++ lambdas or std::function objects to be exposed
 * as JavaScript-callable functions inside QuickJS.
 */
class Callback : public Function {
public:
  /**
   * @brief Constructs a Callback bound to a QuickJS context.
   *
   * @param ctx JSContext used for execution
   */
  Callback(JSContext *ctx) { this->ctx = ctx; }

  /**
   * @brief User-defined function implementation.
   *
   * The callable receives:
   * - this-value
   * - argument array
   *
   * and returns a qjs::Value result.
   */
  std::function<Value(qjs::Value *, qjs::Array *)> fn;

protected:
  /**
   * @brief Executes the stored std::function callback.
   *
   * @param val JavaScript this-value
   * @param arr Argument array
   * @return Result of callback execution
   */
  virtual qjs::Value call(qjs::Value *val, qjs::Array *arr = nullptr) override;

private:
  JSContext *ctx;

protected:
  /**
   * @brief Returns the associated QuickJS context.
   *
   * @return JSContext pointer
   */
  virtual JSContext *context() const override { return ctx; }
};

static JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val,
                                      int argc, JSValueConst *argv, int magic,
                                      JSValueConst *data);

static Array *newEmptyArray(JSContext *);
/**
 * @brief Represents a JavaScript value in C++.
 *
 * This class wraps QuickJS JSValue objects and provides a unified interface
 * for arrays, maps (objects), and functions. It inherits from Array, Map,
 * and Function, allowing array-like, object-like, and callable behavior.
 *
 * It supports creation, assignment, property access, type checking, and
 * automatic memory management for JSValues.
 */
class Value : public Array, public Map, public Function {
protected:
  /**
   * @brief Calls this value as a JavaScript function.
   *
   * @param val The JS `this` value for the function call.
   * @param array Optional array of arguments.
   * @return Result of the function call as a Value.
   *
   * Returns `undefined` if the value is not a function.
   */
  virtual Value call(qjs::Value *val, Array *array = nullptr) override {
    auto ctx = context();
    if (!isFunction()) {
      return Value(ctx, JS_UNDEFINED, true);
    }
    auto func = raw();
    auto size = array->size();
    JSValueConst *argv = new JSValueConst[size];

    for (int i = 0; i < size; i++) {
      argv[i] = array->get(i).raw();
    }

    JSValue result = JS_Call(ctx, func, val->raw(), array->size(), argv);
    delete[] argv;

    return Value(ctx, result, true);
  }

public:
#ifdef QUICKJS_USE_QT
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  qjs::Value get(T name) const;
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool set(T name, qjs::Value value);
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool drop(T name);
  template <typename V, typename T,
            std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool set(T index, V value);
#endif
  /**
   * @brief Deletes a property from an object.
   *
   * @param val Name of the property to delete.
   * @return true if the property was deleted, false otherwise.
   */
  virtual bool drop(const std::string &val) override {
    if (isObject()) {
      auto ctx = context();
      if (JS_DeleteProperty(ctx, raw(),
                            JS_NewAtomLen(ctx, val.c_str(), val.size()),
                            0) == true) {
        return true;
      };
    }
    return false;
  }

  /**
   * @brief Returns the associated context.
   *
   * @return The JS context associated with this Value.
   */
  virtual Context getContext() const override;

  /**
   * @brief Returns the size of an array or object.
   *
   * @return Array length, object property count, or 0 for other types.
   */
  virtual size_t size() const override {
    auto ctx = context();
    if (isArray()) {
      uint32_t len;
      JSValue length_val = JS_GetPropertyStr(ctx, raw(), "length");
      JS_ToUint32(ctx, &len, length_val);
      JS_FreeValue(ctx, length_val);
      return len;
    } else if (isObject()) {
      JSPropertyEnum *props;
      uint32_t prop_count = 0;
      if (JS_GetOwnPropertyNames(ctx, &props, &prop_count, raw(),
                                 JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) ==
          0) {
        js_free(ctx, props);
      }
      return prop_count;
    } else {
      return 0;
    }
  }
  /**
   * @brief Converts JS function arguments to a vector of Values.
   *
   * @param ctx QuickJS context.
   * @param argc Number of arguments.
   * @param argv Array of JSValueConst arguments.
   * @return Vector of wrapped qjs::Value objects.
   */
  static std::vector<qjs::Value> makeVector(JSContext *ctx, int argc,
                                            JSValueConst *argv) {
    std::vector<qjs::Value> out;
    out.reserve(argc);

    for (int i = 0; i < argc; i++) {
      out.emplace_back(ctx, JS_DupValue(ctx, argv[i]), true);
    }

    return out;
  }

  /**
   * @brief Assigns a JSValue to this Value instance.
   *
   * @param ctx QuickJS context.
   * @param value JSValue to assign.
   * @param takeOwnership If true, this object takes ownership of the value.
   */
  void assign(JSContext *ctx, JSValue value, bool takeOwnership) {
    free();
    ctx_ = ctx;
    if (takeOwnership) {
      value_ = value;
    } else {
      value_ = JS_DupValue(ctx, value);
    }
  }

  /**
   * @brief Assigns a numeric value.
   *
   * @param ctx QuickJS context.
   * @param value Double value to wrap.
   */
  template <typename T, std::enable_if_t<std::is_convertible_v<T, double> &&
                                             !std::is_same_v<T, bool>,
                                         int> = 0>
  void assign(JSContext *ctx, T value) {
    assign(ctx, JS_NewFloat64(ctx, (double)value), true);
  }

  /**
   * @brief Assigns a numeric value.
   *
   * @param ctx QuickJS context.
   * @param value Double value to wrap.
   */
  void assign(JSContext *ctx, bool value) {
    assign(ctx, JS_NewBool(ctx, value), true);
  }

  /**
   * @brief Assigns a string value.
   *
   * @param ctx QuickJS context.
   * @param value String to wrap.
   */
  void assign(JSContext *ctx, const std::string &value) {
    JSValue v = JS_NewStringLen(ctx, value.c_str(), value.size());
    assign(ctx, v, true); // consumes v
  }
  void assign(JSContext *ctx, const char *value) {
    JSValue v = JS_NewStringLen(ctx, value, strlen(value));
    assign(ctx, v, true); // consumes v
  }

  /**
   * @brief Assigns a function value.
   *
   * @param ctx QuickJS context.
   * @param value function to wrap.
   */
  void assign(JSContext *ctx, Function *value) {
    assign(ctx, newFunctionValue(ctx, value).rawdup(), true); // consumes v
  }

  /**
   * @brief Assigns a function value.
   *
   * @param ctx QuickJS context.
   * @param value function to wrap.
   */
  void assign(JSContext *ctx, std::function<Value(Value *, Array *)> value) {
    assign(ctx, newFunctionValue(ctx, value).rawdup(), true); // consumes v
  }

  /**
   * @brief Assigns a function value.
   *
   * @param ctx QuickJS context.
   * @param value function to wrap.
   */
  template <typename T, std::enable_if_t<is_template_function<T>, int> = 0>
  void assign(JSContext *ctx, T value) {
    assign(ctx, newFunctionValue(ctx, value).rawdup(), true); // consumes v
  }

  template <typename T> void assign_value(JSContext *ctx, T value);

  template <
      typename T,
      std::enable_if_t<!is_template_function<T> &&
                           !std::is_convertible_v<
                               T, std::function<Value(Value *, Array *)>> &&
                           !std::is_convertible_v<T, Function *> &&
                           !std::is_convertible_v<T, const std::string &> &&
                           !std::is_convertible_v<T, const std::string &> &&
                           !std::is_convertible_v<T, const char *> &&
                           !std::is_convertible_v<T, char *> &&
                           !std::is_convertible_v<T, char[]> &&
                           !std::is_convertible_v<T, double> &&
                           !std::is_convertible_v<T, bool>,
                       int> = 0>
  void assign(JSContext *ctx, T value) {
    assign_value(ctx, value); // consumes v
  }

  /**
   * @brief Assigns an empty JS array.
   *
   * @param ctx QuickJS context.
   */
  void assignArray(JSContext *ctx) { assign(ctx, JS_NewArray(ctx), true); }

  /**
   * @brief Assigns an empty JS object.
   *
   * @param ctx QuickJS context.
   * @param id Optional class ID for the object.
   * @param opaque Optional pointer to associate with the object.
   */
  void assignObject(JSContext *ctx, JSClassID id = JS_INVALID_CLASS_ID,
                    void *opaque = nullptr) {
    if (opaque != nullptr || id != JS_INVALID_CLASS_ID) {
      assign(ctx, JS_NewObjectClass(ctx, id), true);
      JSValue val = rawdup();
      JS_SetOpaque(val, opaque);
      JS_FreeValue(ctx, val);
    } else {
      assign(ctx, JS_NewObject(ctx), true);
    }
  }

#define ASSIGN_MOVE(val)                                                       \
  {                                                                            \
    assign(val.ctx_, val.value_, false);                                       \
    val.free();                                                                \
  }

  template <
      typename T,
      std::enable_if_t<
          !is_array_or_derived<std::decay_t<T>> && is_not_jsvalue<T>, int> = 0>
  Value(JSContext *ctx, T t) {
    assign(ctx, t);
  }

  void assign(const Value &val) { assign(val.ctx_, val.value_, false); }

  Value() {}

  Value(JSContext *ctx, JSValue value, bool takeOwnership) {
    assign(ctx, value, takeOwnership);
  }

  Value &operator=(Value &&val) noexcept {
    ASSIGN_MOVE(val);
    return *this;
  }

  Value &operator=(const Value &val) {
    assign(val);
    return *this;
  }

  Value(const Value &val) { assign(val); };

  Value(Value &&val) noexcept { ASSIGN_MOVE(val); }

  /**
   * @brief Sets a getter and setter function on a property.
   *
   * @param name Property name.
   * @param getter Getter Value.
   * @param setter Setter Value.
   * @param flags JS property flags.
   */
  void setPropertyFunc(const std::string &name, const qjs::Value &getter1,
                       const qjs::Value &setter1,
                       int flags = JS_PROP_ENUMERABLE) {
    auto ctx = context();
    JSAtom atom = JS_NewAtom(ctx, name.c_str());
    JSValue getter = getter1.rawdup();
    JSValue setter = setter1.rawdup();

    JS_DefinePropertyGetSet(ctx, raw(), atom, getter, setter, flags);
    // only free atom
    JS_FreeAtom(ctx, atom);
  }

#ifdef QUICKJS_USE_QT
  template <typename A, typename B, typename T,
            std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  void setPropertyFunc(T name, A getter1, B setter1,
                       int flags = JS_PROP_ENUMERABLE);
#endif

  /**
   * @brief Sets a property with functional getter/setter.
   *
   * @param name Property name.
   * @param getter Getter function.
   * @param setter Setter function.
   * @param flags JS property flags.
   */
  template <typename T, typename D,
            std::enable_if_t<is_template_function<T> && is_template_function<D>,
                             int> = 0>
  void setPropertyFunc(const std::string &name, const T &getter1,
                       const D &setter1, int flags = JS_PROP_ENUMERABLE);

  /**
   * @brief Sets an element in an array.
   *
   * @param index Index in the array.
   * @param value Value to set.
   * @return true on success, false if not an array.
   */
  virtual bool set(size_t index, qjs::Value value) override {
    if (isArray()) {
      auto ctx = context();
      auto val = raw();
      JS_SetPropertyUint32(ctx, val, index, value.rawdup());
      return true;
    } else {
      return false;
    }
  }

  /**
   * @brief Invokes the function with an explicit this-value and argument array.
   *
   * @tparam Array A type derived from qjs::Array
   * @param th The JavaScript `this` value
   * @param xs Argument array
   * @return Result of function execution
   */

  template <typename Array,
            std::enable_if_t<(is_array_or_derived<Array>), int> = 0>
  qjs::Value invoke(qjs::Value *th, Array *xs) {
    return this->Function::invoke(th, xs);
  };

  /**
   * @brief Invokes the function with no arguments and default this-value.
   *
   * @return Result of function execution
   */
  qjs::Value invoke() { return this->Function::invoke(); };

  /**
   * @brief Invokes the function with variadic arguments and explicit
   * this-value.
   *
   * Arguments are automatically packed into a temporary Array implementation.
   *
   * @tparam T Argument types (must not be Array types)
   * @param th The JavaScript `this` value
   * @param xs Variadic arguments to pass to the function
   * @return Result of function execution
   */
  template <typename... T,
            std::enable_if_t<all_not_array<T...>::value, int> = 0>
  qjs::Value invoke(qjs::Value *th, T &&...xs) {
    return this->Function::invoke(th, xs...);
  };

  template <typename Array,
            std::enable_if_t<is_array_or_derived<Array>, int> = 0>
  qjs::Value invoke(const std::string &name, Array *xs) {
    return this->get(name).invoke(this, xs);
  };

  template <typename... T,
            std::enable_if_t<all_not_array<T...>::value, int> = 0>
  qjs::Value invoke(const std::string &th, T &&...xs) {
    auto value = this->get(th);
    return value.invoke(this, xs...);
  };

  template <typename Ret, typename Base, typename... Args>
  bool setMethod(const std::string &name, Ret (Base::*func)(Args...));

  template <typename Ret, typename Base, typename... Args>
  bool setMethod(const std::string &name, Ret (Base::*func)(Args...) const);

  template <typename ClassType, typename FieldType>
  bool setProperty(const std::string & value, FieldType ClassType::*field_ptr, int flags = JS_PROP_ENUMERABLE){
    if (!isObject()){
      return false;
    }
    this->setPropertyFunc(
      value.c_str(), 
      make_getter(field_ptr),
      make_setter(field_ptr),
      flags
    );
    return true;
  }

  bool makeConstructor(qjs::Value *proto) {
    if (!JS_IsFunction(ctx_, value_)) {
      return false;
    }
    JSValue proto_ = proto->raw();
    if (!JS_IsObject(value_)) {
      return false;
    }
    JS_SetConstructorBit(ctx_, value_, 1);

    return JS_SetConstructor(ctx_, value_, proto_) == 0;
  };

  bool makeConstructor(qjs::Value proto) { return makeConstructor(&proto); };

  bool makeConstructor() {
    return makeConstructor(qjs::Value(ctx_, JS_NewObject(ctx_), true));
  }

  bool setConstructor(qjs::Value *func) {
    if (!isObject()) {
      return false;
    }
    return func->makeConstructor((qjs::Value *)this);
  }

  bool setConstructor(qjs::Value proto) { return setConstructor(&proto); };

  template <typename A, std::enable_if_t<is_not_jsvalue<A>, int> = 0>
  bool setConstructor(A fn) {
    qjs::Value val = newFunctionValue(ctx_, fn);
    return setConstructor(val);
  };

#ifdef QUICKJS_USE_QT
  template <
      typename T, typename Array,
      std::enable_if_t<is_array_or_derived<Array> && is_qtstring_or_derived<T>,
                       int> = 0>
  qjs::Value invoke(T name, Array *xs) {
    return this->get(name.toStdString()).invoke(this, xs);
  };

  template <
      typename N, typename... T,
      std::enable_if_t<all_not_array<T...>::value && is_qtstring_or_derived<N>,
                       int> = 0>
  qjs::Value invoke(N name, T &&...xs) {
    auto value = this->get(name.toStdString());
    return value.invoke(this, xs...);
  };

  template <typename N, typename Ret, typename Base, typename... Args,
            std::enable_if_t<is_qtstring_or_derived<N>, int> = 0>
  bool setMethod(N name, Ret (Base::*func)(Args...));

  template <typename N, typename Ret, typename Base, typename... Args,
            std::enable_if_t<is_qtstring_or_derived<N>, int> = 0>
  bool setMethod(N name, Ret (Base::*func)(Args...) const);


  template <typename N, typename ClassType, typename FieldType,
            std::enable_if_t<is_qtstring_or_derived<N>, int> = 0>
  bool setProperty(N value, FieldType ClassType::*field_ptr, int flags = JS_PROP_ENUMERABLE){
    return setProperty(value.toStdString(), field_ptr, flags);
  }

#endif

  /**
   * @brief Sets a property in an object.
   *
   * @param index Property name.
   * @param value Value to set.
   * @return true on success, false if not an object.
   */
  virtual bool set(const std::string &index, qjs::Value value) override {
    if (isObject()) {
      JS_SetPropertyStr(context(), raw(), index.c_str(), value.rawdup());
      return true;
    } else {
      return false;
    }
  }

  /**
   * @brief Gets an element from an array.
   *
   * @param index Index in the array.
   * @return Value at the index, or undefined if not an array.
   */
  virtual qjs::Value get(size_t index) const override {
    auto ctx = context();
    if (isArray()) {
      auto new_val = JS_GetPropertyUint32(ctx, raw(), index);
      return Value(ctx, new_val, true);
    } else {
      return Value(ctx, JS_UNDEFINED, true);
    }
  }

  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  }

  template <typename T> bool set(std::string index, T value) {
    return set(index, qjs::Value(context(), value));
  }

  /**
   * @brief Gets a property from an object.
   *
   * @param name Property name.
   * @return Value of the property, or undefined if not an object.
   */
  virtual qjs::Value get(const std::string &name) const override {
    auto ctx = context();
    if (isObject()) {
      auto new_val = JS_GetPropertyStr(ctx, raw(), name.c_str());
      return Value(ctx, new_val, true);
    } else {
      return Value(ctx, JS_UNDEFINED, true);
    }
  }

  typedef JSClassID ClassID;

  void *as(ClassID classid) {
    if (classid == JS_INVALID_CLASS_ID) {
      return nullptr;
    }
    return JS_GetOpaque(value_, classid);
  }

  bool is(ClassID classid) { return as(classid) != nullptr; }

  ClassID getClassID() { return JS_GetClassID(value_); }

  void *asOpaque() { return as(getClassID()); }

  bool isOpaque() { return is(getClassID()); }

  template <typename T> bool is() const;

  template <typename T> T as() const;

  bool isException() const { return JS_IsException(raw()); }

  bool isUndefined() const { return JS_IsUndefined(raw()); }

  bool isNull() const { return JS_IsNull(raw()); }

  bool isBool() const { return JS_IsBool(raw()); }

  bool isNumber() const { return JS_IsNumber(raw()); }

  bool isString() const { return JS_IsString(raw()); }

  bool isObject() const { return JS_IsObject(raw()) && !isArray(); }

  bool isFunction() const { return JS_IsFunction(context(), raw()); }

  bool isArray() const { return JS_IsArray(raw()); }

  bool isSymbol() const { return JS_VALUE_GET_TAG(raw()) == JS_TAG_SYMBOL; }

  bool isBigInt() const { return JS_VALUE_GET_TAG(raw()) == JS_TAG_BIG_INT; }

  virtual ~Value() { free(); }

  template <typename F> friend auto wrap_method(F member_ptr, JSContext *ctx);
  friend void throwIfException(JSContext *ctx, qjs::Value &value);
  friend class PointerArray;
  friend class Context;
  friend JSValue js_function_trampoline(JSContext *ctx, JSValueConst this_val,
                                        int argc, JSValueConst *argv, int magic,
                                        JSValueConst *data);

protected:
  JSValue rawdup() const { return JS_DupValue(ctx_, value_); }
  JSValue raw() const { return value_; }

  virtual JSContext *context() const override { return ctx_; }
  void free() {
    if (ctx_ != nullptr) {
      JS_FreeValue(ctx_, value_);
      ctx_ = nullptr;
    }
  }
  JSContext *ctx_ = nullptr;
  JSValue value_ = JS_UNDEFINED;
};

[[noreturn]]
void throw_qjs_exception(const std::string &error);

template <bool HasVal, bool HasPointer, typename F, typename Tuple,
          size_t... Is>
auto invoke_array(F &&lambda, Value *val, const Array *arr,
                  std::index_sequence<Is...>) {
  if constexpr (HasVal) {
    return std::invoke(
        std::forward<F>(lambda), val,
        arr->get(Is).template as<std::tuple_element_t<Is + 1, Tuple>>()...);
  } else if constexpr (HasPointer) {
    using RawType = std::tuple_element_t<0, Tuple>;

    RawType pointer = nullptr;
    if (val != nullptr) {

      pointer = static_cast<RawType>(val->asOpaque());

      if constexpr (!std::is_void_v<std::remove_pointer_t<RawType>>) {
        if (pointer == nullptr) {
          throw_qjs_exception("Type safety violation: Target object pointer is "
                              "invalid or mismatched type.");
        }
      }
    }
    return std::invoke(
        std::forward<F>(lambda), pointer,
        arr->get(Is).template as<std::tuple_element_t<Is + 1, Tuple>>()...);
  } else {
    return std::invoke(
        std::forward<F>(lambda),
        arr->get(Is).template as<std::tuple_element_t<Is, Tuple>>()...);
  }
}

template <typename T> struct drop_first_element;

template <typename First, typename... Rest>
struct drop_first_element<std::tuple<First, Rest...>> {
  using type = std::tuple<Rest...>;
};

template <> struct drop_first_element<std::tuple<>> {
  using type = std::tuple<>;
};

template <typename T, std::enable_if_t<is_not_jsvalue<T>, int> = 0>
qjs::Value convertToValue(T t, JSContext *ctx) {
  return qjs::Value(ctx, t);
}

template <typename T, std::enable_if_t<!is_not_jsvalue<T>, int> = 0>
qjs::Value convertToValue(T t, JSContext *ctx) {
  return t;
}

template <typename T, typename F, typename JsArgsTuple, size_t... Is>
auto invoke_method(F member_ptr, T *val, const Array *arr,
                   std::index_sequence<Is...>) {
  return std::invoke(
      member_ptr, val,
      arr->get(Is).template as<std::tuple_element_t<Is, JsArgsTuple>>()...);
}

template <typename F> auto wrap_method(F member_ptr, JSContext *ctx) {
  static_assert(
      std::is_member_function_pointer_v<std::decay_t<F>>,
      "wrap_method can only be instantiated with a member function pointer.");

  using DecayedF = std::decay_t<F>;
  using base_type = typename callable_traits<DecayedF>::base_type;
  using return_type = typename callable_traits<DecayedF>::return_type;
  using js_args_tuple = typename callable_traits<DecayedF>::args_tuple;
  constexpr size_t js_args_count = std::tuple_size_v<js_args_tuple>;

  return [ctx, member_ptr](Value *val, Array *arr) mutable -> Value {
    if (val == nullptr || arr == nullptr || ctx == nullptr) {
      throw_qjs_exception(
          "Critical: Null execution context passed from QuickJS runtime.");
    }
    auto id = JS_GetClassID(val->raw());
    if (id == JS_INVALID_CLASS_ID) {
      throw_qjs_exception("Critical: Invalid ClassID");
    }
    if (arr->size() < js_args_count) {
      throw_qjs_exception("Argument mismatch: JavaScript provided fewer "
                          "parameters than required.");
    }
    auto base = static_cast<base_type *>(val->as(id));
    if (base == nullptr) {
      throw_qjs_exception("Type safety violation: Target object pointer is "
                          "invalid or mismatched type.");
    }
    try {
      if constexpr (std::is_void_v<return_type>) {
        invoke_method<base_type, DecayedF, js_args_tuple>(
            member_ptr, base, arr, std::make_index_sequence<js_args_count>{});
        return qjs::Value(ctx, JS_UNDEFINED, true);
      } else {
        return convertToValue(invoke_method<base_type, DecayedF, js_args_tuple>(
                                  member_ptr, base, arr,
                                  std::make_index_sequence<js_args_count>{}),
                              ctx);
      }
    } catch (const std::exception &e) {
      throw_qjs_exception(e.what());
    } catch (...) {
      throw_qjs_exception(
          "Unknown native exception occurred during method execution.");
    }
  };
}

template <typename F> auto wrap_lambda(F &&lambda, JSContext *ctx) {
  using DecayedF = std::decay_t<F>;
  using args_tuple = typename callable_traits<DecayedF>::args_tuple;

  using return_type = typename callable_traits<DecayedF>::return_type;
  constexpr size_t total_args = callable_traits<DecayedF>::args_count;
  constexpr bool has_val_param = []() {
    if constexpr (total_args > 0) {
      return std::is_same_v<std::tuple_element_t<0, args_tuple>, Value *>;
    } else {
      return false;
    }
  }();
  constexpr bool has_pointer_param = []() {
    if constexpr (total_args > 0 && !has_val_param) {
      return std::is_pointer_v<std::tuple_element_t<0, args_tuple>>;
    } else {
      return false;
    }
  }();
  constexpr size_t js_args_count =
      (has_val_param || has_pointer_param) ? (total_args - 1) : total_args;

  return [ctx, lambda = std::forward<F>(lambda)](Value *val,
                                                 Array *arr) mutable -> Value {
    if (arr->size() >= js_args_count) {
      if constexpr (std::is_void_v<return_type>) {
        invoke_array<has_val_param, has_pointer_param, F, args_tuple>(
            lambda, val, arr, std::make_index_sequence<js_args_count>{});
        return qjs::Value(ctx, JS_UNDEFINED, true);
      } else {
        return convertToValue(
            invoke_array<has_val_param, has_pointer_param, F, args_tuple>(
                lambda, val, arr, std::make_index_sequence<js_args_count>{}),
            ctx);
      }
    } else {
      throw_qjs_exception("Invalid argument count");
    }
  };
}

class VectorArray : public Array, public std::vector<Value> {
public:
  virtual ~VectorArray() override { this->clear(); }
  virtual Value get(size_t index) const override {
    return std::vector<Value>::at(index);
  };
  virtual bool set(size_t index, Value value) override {
    auto count = size();
    if (count <= index) {
      this->std::vector<Value>::reserve(index * 2);
      auto ctx = context();
      while (count < index) {
        this->std::vector<Value>::push_back(
            qjs::Value(ctx, JS_UNDEFINED, true));
        count++;
      }
      this->std::vector<Value>::push_back(value);
    } else {
      this->std::vector<Value>::at(index) = value;
    }
    return true;
  };
  virtual size_t size() const override { return std::vector<Value>::size(); };
  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  };
  VectorArray(JSContext *ctx) { this->ctx = ctx; }

private:
  JSContext *ctx;

protected:
  virtual JSContext *context() const override { return ctx; }
};

Array *
newEmptyArray(JSContext *context) { // NOLINT(misc-definitions-in-headers)
  return new VectorArray(context);
}

template <typename T> bool Array::set(size_t index, T value) {
  return set(index, qjs::Value(context(), value));
}

template <typename T> bool Map::set(const std::string &index, T value) {
  return set(index, qjs::Value(context(), value));
}

qjs::Value Callback::call(Value *val, // NOLINT(misc-definitions-in-headers)
                          Array *array) {
  bool delete_array = array == nullptr;
  if (delete_array) {
    array = newEmptyArray(ctx);
  }
  auto ret = fn(val, array);
  if (delete_array) {
    delete array;
  }
  return ret;
};

class PointerArray : public Array {
public:
  virtual ~PointerArray() {};
  PointerArray(JSContext *ctx, int argc, JSValueConst *argv) {
    this->ctx = ctx;
    this->length = argc;
    this->array = argv;
  }
  virtual Value get(size_t index) const override {
    auto ctx = context();
    if (index >= length) {
      return Value(ctx, JS_UNDEFINED, true);
    } else {
      return Value(ctx, array[index], false);
    }
  };
  virtual bool set(size_t index, Value value) override {
    if (index >= length) {
      return false;
    } else {
      array[index] = value.rawdup();
      return true;
    }
  };
  virtual JSContext *context() const override { return ctx; };
  virtual size_t size() const override { return length; };

  template <typename T> bool set(size_t index, T value) {
    return set(index, qjs::Value(context(), value));
  }

private:
  size_t length = 0;
  JSValueConst *array;
  JSContext *ctx;
};

void ClassWrapperFinalize(JSRuntime *rt, JSValueConst val);

struct ClassWrapper {
public:
  JSClassID id;
  std::function<void(void *)> finalize;
  ClassWrapper(const std::string &name) {
    def.class_name = strdup(name.c_str());
    def.finalizer = &ClassWrapperFinalize;
    id = JS_INVALID_CLASS_ID;
  }
  ~ClassWrapper() { free((void *)def.class_name); }
  static std::shared_ptr<ClassWrapper> newWrapper(const std::string &name) {
    return std::make_shared<ClassWrapper>(name);
  }
  JSClassDef def;
};

class QuickJS_CppClasses {
public:
  friend QuickJS_CppClasses *getClasses(JSRuntime *rt);
  JSClassID function_class;
  std::unordered_map<JSClassID, std::shared_ptr<ClassWrapper>> finalizer_map;
  ~QuickJS_CppClasses() { free(); }

private:
  void free() { finalizer_map.clear(); }
};

QuickJS_CppClasses *getClasses(JSRuntime *rt);

void ClassWrapperFinalize( // NOLINT(misc-definitions-in-headers)
    JSRuntime *rt, JSValueConst val) {
  auto classes = getClasses(rt);
  auto classid = JS_GetClassID(val);
  if (classid == JS_INVALID_CLASS_ID) {
    return;
  }
  auto opaque = JS_GetOpaque(val, classid);
  if (opaque == nullptr) {
    return;
  }
  std::shared_ptr<ClassWrapper> wrapper = nullptr;
  {
    auto iter = classes->finalizer_map.find(classid);
    if (iter != classes->finalizer_map.end()) {
      wrapper = iter->second;
    }
  }
  if (wrapper != nullptr) {
    auto fn = wrapper->finalize;
    if (fn != nullptr) {
      fn(opaque);
    }
  }
};

qjs::Value Function::invoke() { // NOLINT(misc-definitions-in-headers)
  return this->invoke(nullptr, (Array *)nullptr);
}

template <typename Array, std::enable_if_t<(is_array_or_derived<Array>), int>>
qjs::Value Function::invoke(Value *th, Array *Ts) {
  bool delete_array = Ts == nullptr;
  bool delete_value = th == nullptr;
  qjs::Array *arr;

  if (delete_array) {
    arr = newEmptyArray(this->context());
  } else {
    arr = Ts;
  }

  if (delete_value) {
    th = new qjs::Value(this->context(), JS_UNDEFINED, false);
  }

  auto ret = this->call(th, arr);
  if (delete_value) {
    delete th;
  }
  if (delete_array) {
    delete arr;
  }
  return ret;
};

template <typename... T, std::enable_if_t<all_not_array<T...>::value, int>>
qjs::Value Function::invoke(qjs::Value *th, T &&...xs) {
  auto ctx = this->context();

  VectorArray arr(ctx);
  arr.reserve(sizeof...(T));

  // Expand arguments safely
  (arr.emplace_back(ctx, std::forward<T>(xs)), ...);

  return this->invoke(th, &arr);
}

static void function_finalizer(JSRuntime *rt, JSValue val);

static JSClassDef function_def = {"qr243vbi_function", function_finalizer};

static void finalize_pointer(void *ptr,
                             std::function<void(void *)> fn = nullptr);

QuickJS_CppClasses *
getClasses(JSRuntime *rt) { // NOLINT(misc-definitions-in-headers)
  auto cls = (QuickJS_CppClasses *)JS_GetRuntimeOpaque(rt);
  if (cls == nullptr) {
    cls = new QuickJS_CppClasses();
    finalize_pointer((void *)cls, [](void *p) -> void {
      auto cls = static_cast<QuickJS_CppClasses *>(p);
      cls->free();
    });
    JSClassID clsID = 0;
    JS_NewClassID(rt, &clsID);
    JS_NewClass(rt, clsID, &function_def);
    JS_SetRuntimeOpaque(rt, cls);
    cls->function_class = clsID;
  }
  return cls;
}

QuickJS_CppClasses *
getClasses(JSContext *rt) { // NOLINT(misc-definitions-in-headers)
  return getClasses(JS_GetRuntime(rt));
}

JSValue js_function_trampoline(
    JSContext *ctx,
    JSValueConst this_val, // NOLINT(misc-definitions-in-headers)
    int argc, JSValueConst *argv, int magic, JSValueConst *data) {
  JSClassID class_id = getClasses(ctx)->function_class;
  qjs::Function *cb = (qjs::Function *)JS_GetOpaque(data[0], class_id);
  std::shared_ptr<PointerArray> ptr =
      std::make_shared<PointerArray>(ctx, argc, argv);

  JSValue retvaljs;
  {
    auto thisval = Value(ctx, this_val, false);
    {
      auto retval = cb->invoke(&thisval, ptr.get());
      retvaljs = retval.rawdup();
    }
  }

  return retvaljs;
}

void function_finalizer(JSRuntime *rt, JSValue val) {
  QuickJS_CppClasses *cls = (QuickJS_CppClasses *)JS_GetRuntimeOpaque(rt);
  if (cls != nullptr) {
    Function *opaque = (Function *)JS_GetOpaque(val, cls->function_class);
    if (opaque != nullptr) {
      delete opaque;
    }
  }
}

class Runtime {
public:
  Runtime() {
    rt_ = JS_NewRuntime();
    if (!rt_) {
      throw std::runtime_error("JS_NewRuntime failed");
    }
  }

  ~Runtime() { this->free(); }

  Runtime(const Runtime &) = delete;
  Runtime &operator=(const Runtime &) = delete;

  Runtime(Runtime &&other) noexcept : rt_(other.rt_) { other.rt_ = nullptr; }

  Runtime &operator=(Runtime &&other) noexcept {
    if (this != &other) {
      if (rt_) {
        free();
      }
      rt_ = other.rt_;
      other.rt_ = nullptr;
    }
    return *this;
  }

  JSRuntime *get() const { return rt_; }

private:
  JSRuntime *rt_{};
  void free() {
    if (rt_) {
      auto data = JS_GetRuntimeOpaque(rt_);
      JS_FreeRuntime(rt_);
      finalize_pointer(data);
    }
  }
};

void finalize_pointer(void *ptr, std::function<void(void *)> fn) {
  static std::map<void *, std::function<void(void *)>> finalizers;
  if (ptr != nullptr) {
    if (fn == nullptr) {
      auto iter = finalizers.find(ptr);
      if (iter != finalizers.end()) {
        if (iter->second != nullptr) {
          iter->second(ptr);
        }
        finalizers.erase(iter);
      }
      free(ptr);
    } else {
      finalizers[ptr] = fn;
    }
  }
}

class Context {
public:
  explicit Context() {
    rt_ = JS_NewRuntime();
    if (!rt_)
      throw std::runtime_error("JS_NewRuntime failed");
    ctx_ = JS_NewContext(rt_);
    if (!ctx_)
      throw std::runtime_error("JS_NewContext failed");
  }
  explicit Context(JSContext *ctx, bool takeOwnership) {
    this->owned = takeOwnership;
    this->ctx_ = ctx;
  }
  explicit Context(Runtime &runtime) : Context(runtime.get(), false) {}
  explicit Context(JSRuntime *rt_, bool takeOwnership) {
    ctx_ = JS_NewContext(rt_);
    if (!ctx_)
      throw std::runtime_error("JS_NewContext failed");
    if (takeOwnership) {
      this->rt_ = rt_;
    }
  }

  ~Context() { free(); }

#define CONTEXT_ASSIGN_MOVE(val)                                               \
  this->ctx_ = val.ctx_;                                                       \
  this->rt_ = val.rt_;                                                         \
  this->owned = val.owned;                                                     \
  val.owned = false;

  Context &operator=(Context &&val) noexcept {
    CONTEXT_ASSIGN_MOVE(val);
    return *this;
  }

  Context(Context &&val) noexcept { CONTEXT_ASSIGN_MOVE(val); }

  JSContext *get() const { return ctx_; }

  std::shared_ptr<ClassWrapper> newClass(const std::string &name, Value proto) {
    auto wrapper = ClassWrapper::newWrapper(name);
    wrapper->id = 0;
    auto runtime = JS_GetRuntime(ctx_);
    JS_NewClassID(runtime, &wrapper->id);
    JS_NewClass(runtime, wrapper->id, &wrapper->def);
    JSValue pr;
    if (!proto.isObject()) {
      proto = newObject();
    }
    pr = JS_NewObjectProto(ctx_, proto.raw());
    JS_SetClassProto(ctx_, wrapper->id, pr);
    auto classes = getClasses(runtime);
    classes->finalizer_map[wrapper->id] = wrapper;
    return wrapper;
  }

  std::shared_ptr<ClassWrapper> newClass(const std::string &name) {
    return newClass(name, newObject());
  }

  JSClassID newClassID(const std::string &name, Value proto) {
    return newClass(name, proto)->id;
  }

  JSClassID newClassID(const std::string &name) { return newClass(name)->id; }

  template <typename T> qjs::Value newValue(T t) { return qjs::Value(ctx_, t); }

  Value getClassProto(JSClassID id) {
    JSValue proto = JS_GetClassProto(ctx_, id);
    return Value(ctx_, proto, true);
  }

  qjs::Value newFunction(qjs::Function *fn) {
    if (qjs::Value *d = dynamic_cast<qjs::Value *>(fn)) {
      return *d;
    }
    JSContext *ctx = this->ctx_;
    auto class_id = getClasses(ctx)->function_class;
    JSValue proto = JS_GetClassProto(ctx, class_id);
    if (JS_IsUndefined(proto)) {
      proto = JS_NewObject(ctx);
      JS_SetClassProto(ctx, class_id, proto);
    }

    JSValue data_obj = JS_NewObjectClass(ctx, class_id);

    JS_SetOpaque(data_obj, fn);

    JSValue data[1] = {data_obj};

    JSValue func = JS_NewCFunctionData(ctx, js_function_trampoline,
                                       0, // length
                                       0, // magic
                                       1, // data count
                                       data);

    JS_FreeValue(ctx, data_obj);

    return Value(ctx, func, true);
  }

  qjs::Value newFunction(std::function<Value(Value *, Array *)> fn) {
    auto call = new Callback(this->ctx_);
    call->fn = fn;
    return newFunction(call);
  }

  template <typename T, std::enable_if_t<is_template_function<T>, int> = 0>
  qjs::Value newFunction(T fn) {
    std::function<Value(Value *, Array *)> fn1 = wrap_lambda(fn, this->ctx_);
    return newFunction(fn1);
  }

  template <typename T>
  qjs::Value newConstructor(T fn, const qjs::Value &proto) {
    auto func = newFunction(fn);
    func.setConstructor(proto);
    return func;
  }

  qjs::Value getGlobal() {
    return qjs::Value(ctx_, JS_GetGlobalObject(ctx_), true);
  }

  qjs::Value getGlobal(const std::string &name) {
    auto global = getGlobal();
    return global.get(name);
  }

  bool setGlobal(const std::string &name, const qjs::Value &val) {
    auto global = getGlobal();
    return global.set(name, val);
  };

  bool dropGlobal(const std::string &name) {
    auto global = getGlobal();
    return global.drop(name);
  }

  template <typename T, std::enable_if_t<is_not_jsvalue<T>, int> = 0>
  bool setGlobal(const std::string &name, T val) {
    return setGlobal(name, qjs::Value(ctx_, val));
  };

#ifdef QUICKJS_USE_QT

  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  qjs::Value getGlobal(T name) {
    return getGlobal(name.toStdString());
  }

  template <typename V, typename T,
            std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool setGlobal(T name, V val) {
    return setGlobal(name.toStdString(), val);
  };

  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  bool dropGlobal(T name) {
    return dropGlobal(name.toStdString());
  }
#endif

  qjs::Value newObject(JSClassID id = JS_INVALID_CLASS_ID,
                       void *opaque = nullptr) {
    qjs::Value val;
    val.assignObject(ctx_, id, opaque);
    return val;
  }

  qjs::Value newArray() {
    qjs::Value val;
    val.assignArray(ctx_);
    return val;
  }

  qjs::Value eval(const std::string &code,
                  const std::string &filename = "<eval>") {
    return Value(ctx_,
                 JS_Eval(ctx_, code.c_str(), code.size(), filename.c_str(),
                         JS_EVAL_TYPE_GLOBAL),
                 true);
  }

#ifdef QUICKJS_USE_QT
  template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int> = 0>
  qjs::Value eval(T code, const std::string &filename = "<eval>") {
    return eval(code.toStdString(), filename);
  }
  template <typename B, std::enable_if_t<is_qtstring_or_derived<B>, int> = 0>
  qjs::Value eval(const std::string &code, B filename) {
    return eval(code, filename.toStdString());
  }
  template <
      typename T, typename B,
      std::enable_if_t<is_qtstring_or_derived<T> && is_qtstring_or_derived<B>,
                       int> = 0>
  qjs::Value eval(T code, B filename) {
    return eval(code.toStdString(), filename.toStdString());
  }
#endif

private:
  JSContext *ctx_ = nullptr;
  JSRuntime *rt_ = nullptr;
  bool owned = true;
  void free() {
    if (owned) {
      if (ctx_ != nullptr) {
        void *data = JS_GetContextOpaque(ctx_);
        JS_FreeContext(ctx_);
        finalize_pointer(data);
      }
      if (rt_ != nullptr) {
        void *data = JS_GetRuntimeOpaque(rt_);
        JS_FreeRuntime(rt_);
        finalize_pointer(data);
      }
    }
  }
};

Context Value::getContext() const { // NOLINT(misc-definitions-in-headers)
  return this->Array::getContext();
}

template <
    typename T, typename D,
    std::enable_if_t<is_template_function<T> && is_template_function<D>, int>>
void Value::setPropertyFunc(const std::string &name, const T &getter1,
                            const D &setter1, int flags) {
  auto ctx = getContext();
  qjs::Value getter = ctx.newFunction(wrap_lambda(getter1, ctx_));
  qjs::Value setter = ctx.newFunction(wrap_lambda(setter1, ctx_));

  setPropertyFunc(name, getter, setter, flags);
};

#ifdef QUICKJS_USE_QT
template <typename A, typename B, typename T,
          std::enable_if_t<is_qtstring_or_derived<T>, int>>
void Value::setPropertyFunc(T name, A getter1, B setter1, int flags) {
  setPropertyFunc(name.toStdString(), getter1, setter1, flags);
}
#endif

template <> inline bool Value::is<std::vector<Value>>() const {
  return isArray();
}

template <> inline bool Value::is<std::map<std::string, Value>>() const {
  return isObject();
}

template <> inline bool Value::is<std::nullptr_t>() const { return isNull(); }

template <> inline bool Value::is<bool>() const { return isBool(); }

template <> inline bool Value::is<int32_t>() const { return isNumber(); }

template <> inline bool Value::is<int64_t>() const { return isNumber(); }

template <> inline bool Value::is<double>() const { return isNumber(); }

template <> inline bool Value::is<std::string>() const { return isString(); }

class Exception : public std::runtime_error {
public:
  explicit Exception(const std::string &error) : std::runtime_error(error) {}

  explicit Exception(JSContext *ctx, const std::string &error = "")
      : std::runtime_error(extract(ctx, error)) {}

private:
  static std::string extract(JSContext *ctx, const std::string &error) {
    JSValue exc = JS_GetException(ctx);

    JSAtom atom = JS_NewAtom(ctx, "stack");
    std::string result;

    JSValue stack = JS_GetProperty(ctx, exc, atom);

    JS_FreeAtom(ctx, atom);

    if (!JS_IsUndefined(stack)) {
      const char *s = JS_ToCString(ctx, stack);

      if (s) {
        result = s;
        JS_FreeCString(ctx, s);
      }
    }

    {
      const char *s = JS_ToCString(ctx, exc);

      if (s) {
        result += s;
        JS_FreeCString(ctx, s);
      }
    }

    JS_FreeValue(ctx, stack);
    JS_FreeValue(ctx, exc);
    if (error.empty()) {
      return result;
    } else {
      return error + " " + result;
    }
  }
};

template <> inline Value Value::as<Value>() const { return *this; }

template <> inline Value *Value::as<Value *>() const { return (Value *)this; }

template <> inline std::string Value::as<std::string>() const {
  auto ctx_ = context();
  const char *str = JS_ToCString(context(), raw());

  if (!str)
    throw Exception(ctx_);

  std::string result(str);

  JS_FreeCString(ctx_, str);

  return result;
}

template <> inline bool Value::as<bool>() const {
  auto value_ = raw();
  auto ctx_ = context();
  int r = JS_ToBool(ctx_, value_);

  if (r < 0)
    throw Exception(ctx_);

  return r;
}

template <> inline int32_t Value::as<int32_t>() const {
  int32_t v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToInt32(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

template <> inline int64_t Value::as<int64_t>() const {
  int64_t v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToInt64(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

template <> inline double Value::as<double>() const {
  double v;
  auto value_ = raw();
  auto ctx_ = context();

  if (JS_ToFloat64(ctx_, &v, value_))
    throw Exception(ctx_);

  return v;
}

inline void throwIfException(JSContext *ctx, JSValueConst value) {
  if (!JS_IsException(value)) {
    return;
  }

  throw qjs::Exception(ctx);
}

Context
ContextGetter::getContext() const { // NOLINT(misc-definitions-in-headers)
  return qjs::Context(this->context(), false);
}

inline void throwIfException(JSContext *ctx, qjs::Value &value) {
  throwIfException(ctx, value.raw());
}

Value newFunctionValue(JSContext *ctx, qjs::Function *func) {
  qjs::Context context(ctx, false);
  return context.newFunction(func);
};

Value newFunctionValue(JSContext *ctx,
                       std::function<Value(Value *, Array *)> func) {
  qjs::Context context(ctx, false);
  return context.newFunction(func);
};

template <typename T, std::enable_if_t<is_template_function<T>, int>>
Value newFunctionValue(JSContext *ctx, T fn) {
  qjs::Context context(ctx, false);
  return context.newFunction(fn);
};

void throw_qjs_exception( // NOLINT(misc-definitions-in-headers)
    const std::string &error) {
  throw qjs::Exception(error);
};

template <typename Ret, typename Base, typename... Args>
bool qjs::Value::setMethod(const std::string &name,
                           Ret (Base::*func)(Args...)) {
  auto method = qjs::wrap_method(func, ctx_);
  return set(name, method);
}

template <typename Ret, typename Base, typename... Args>
bool qjs::Value::setMethod(const std::string &name,
                           Ret (Base::*func)(Args...) const) {
  auto method = qjs::wrap_method(func, ctx_);
  return set(name, method);
}

#ifdef QUICKJS_USE_QT

template <typename N, typename Ret, typename Base, typename... Args,
          std::enable_if_t<is_qtstring_or_derived<N>, int>>
bool qjs::Value::setMethod(N name, Ret (Base::*func)(Args...)) {
  return this->setMethod(name.toStdString(), func);
};

template <typename N, typename Ret, typename Base, typename... Args,
          std::enable_if_t<is_qtstring_or_derived<N>, int>>
bool qjs::Value::setMethod(N name, Ret (Base::*func)(Args...) const) {
  return this->setMethod(name.toStdString(), func);
};

template <> inline bool Value::is<QString>() const { return isString(); }
template <> inline QString Value::as<QString>() const {
  return QString::fromStdString(as<std::string>());
}
template <>
inline void Value::assign_value<QString>(JSContext *ctx, QString value) {
  assign(ctx, value.toStdString());
}

QVariant jsValueToQVariant(JSContext *ctx, JSValueConst val);

QVariantMap jsValueToQVariantMap(JSContext *ctx, JSValueConst val) {
  QVariantMap map;
  if (JS_IsObject(val)) {
    if (!JS_IsArray(val)) {
      JSPropertyEnum *props = nullptr;
      uint32_t propCount = 0;
      if (JS_GetOwnPropertyNames(ctx, &props, &propCount, val,
                                 JS_GPN_STRING_MASK | JS_GPN_SYMBOL_MASK) >=
          0) {
        for (uint32_t i = 0; i < propCount; ++i) {
          JSAtom atom = props[i].atom;
          const char *keyStr = JS_AtomToCString(ctx, atom);

          JSValue propVal = JS_GetProperty(ctx, val, atom);
          map.insert(QString::fromUtf8(keyStr),
                     jsValueToQVariant(ctx, propVal));

          JS_FreeValue(ctx, propVal);
          JS_FreeCString(ctx, keyStr);
          JS_FreeAtom(ctx, atom);
        }
        js_free(ctx, props);
      }
    }
  }
  return map;
}

QVariantList jsValueToQVariantList(JSContext *ctx, JSValueConst val) {
  QVariantList list;
  if (JS_IsObject(val)) {
    if (JS_IsArray(val)) {

      JSValue lenVal = JS_GetPropertyStr(ctx, val, "length");
      int64_t length = 0;
      JS_ToInt64(ctx, &length, lenVal);
      JS_FreeValue(ctx, lenVal);

      for (int64_t i = 0; i < length; ++i) {
        JSValue element = JS_GetPropertyUint32(ctx, val, i);
        list.append(jsValueToQVariant(ctx, element));
        JS_FreeValue(ctx, element); // Decrement reference count
      }
    }
  }
  return list;
}

QVariant jsValueToQVariant(JSContext *ctx, JSValueConst val) {
  if (JS_IsNull(val) || JS_IsUndefined(val)) {
    return QVariant();
  }
  if (JS_IsBool(val)) {
    return QVariant(static_cast<bool>(JS_ToBool(ctx, val)));
  }
  if (JS_IsNumber(val)) {
    double d;
    JS_ToFloat64(ctx, &d, val);
    // Safely determine if it can be represented cleanly as an integer
    if (d == static_cast<int>(d)) {
      return QVariant(static_cast<int>(d));
    }
    return QVariant(d);
  }
  if (JS_IsString(val)) {
    size_t len;
    const char *str = JS_ToCStringLen(ctx, &len, val);
    QString qstr = QString::fromUtf8(str, static_cast<int>(len));
    JS_FreeCString(ctx, str);
    return QVariant(qstr);
  }

  // Complex Structures (Arrays and Objects)
  if (JS_IsObject(val)) {
    if (JS_IsArray(val)) {
      return QVariant(jsValueToQVariantList(ctx, val));
    } else {
      return QVariant(jsValueToQVariantMap(ctx, val));
    }
  }

  return QVariant();
}

JSValue qvariantToJSValue(JSContext *ctx, const QVariant &var);

JSValue qvariantMapToJSValue(JSContext *ctx, const QVariantMap &map) {
  JSValue jsObj = JS_NewObject(ctx);

  QMapIterator<QString, QVariant> i(map);
  while (i.hasNext()) {
    i.next();
    QByteArray keyUtf8 = i.key().toUtf8();
    JSValue propVal = qvariantToJSValue(ctx, i.value());
    JS_SetPropertyStr(ctx, jsObj, keyUtf8.constData(), propVal);
  }
  return jsObj;
}

JSValue qvariantListToJSValue(JSContext *ctx, const QVariantList &list) {
  JSValue jsArray = JS_NewArray(ctx);

  for (int i = 0; i < list.size(); ++i) {
    JSValue element = qvariantToJSValue(ctx, list.at(i));
    JS_SetPropertyUint32(ctx, jsArray, i, element);
  }
  return jsArray;
}

JSValue qvariantToJSValue(JSContext *ctx, const QVariant &var) {
  if (!var.isValid() || var.isNull()) {
    return JS_NULL;
  }

  switch (var.typeId()) {
  case QMetaType::Bool:
    return JS_NewBool(ctx, var.toBool());

  case QMetaType::Int:
  case QMetaType::UInt:
    return JS_NewInt32(ctx, var.toInt());

  case QMetaType::LongLong:
  case QMetaType::ULongLong:
  case QMetaType::Double:
    return JS_NewFloat64(ctx, var.toDouble());

  case QMetaType::QString: {
    QByteArray utf8 = var.toString().toUtf8();
    return JS_NewStringLen(ctx, utf8.constData(), utf8.size());
  }

  case QMetaType::QVariantList: {
    QVariantList list = var.toList();
    return qvariantListToJSValue(ctx, list);
  }

  case QMetaType::QVariantMap: {
    QVariantMap map = var.toMap();
    return qvariantMapToJSValue(ctx, map);
  }

  default: {
    QByteArray fallbackUtf8 = var.toString().toUtf8();
    return JS_NewStringLen(ctx, fallbackUtf8.constData(), fallbackUtf8.size());
  }
  }
}

template <> inline bool Value::is<QVariant>() const { return true; }

template <> inline QVariant Value::as<QVariant>() const {
  return jsValueToQVariant(ctx_, value_);
}
template <>
inline void Value::assign_value<QVariant>(JSContext *ctx, QVariant value) {
  assign(ctx, qvariantToJSValue(ctx, value), true);
}

template <> inline bool Value::is<QVariantMap>() const { return isObject(); }

template <> inline QVariantMap Value::as<QVariantMap>() const {
  return jsValueToQVariantMap(ctx_, value_);
}
template <>
inline void Value::assign_value<QVariantMap>(JSContext *ctx,
                                             QVariantMap value) {
  assign(ctx, qvariantMapToJSValue(ctx, value), true);
}

template <> inline bool Value::is<QVariantList>() const { return isArray(); }

template <> inline QVariantList Value::as<QVariantList>() const {
  return jsValueToQVariantList(ctx_, value_);
}
template <>
inline void Value::assign_value<QVariantList>(JSContext *ctx,
                                              QVariantList value) {
  assign(ctx, qvariantListToJSValue(ctx, value), true);
}

template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
qjs::Value Map::get(T name) const {
  return this->get(name.toStdString());
};
template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Map::set(T name, qjs::Value value) {
  return this->set(name.toStdString(), value);
};

template <typename V, typename T,
          std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Map::set(T name, V value) {
  return this->set(name.toStdString(), value);
};
template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Map::drop(T name) {
  return this->drop(name.toStdString());
};

template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
qjs::Value Value::get(T name) const {
  return this->get(name.toStdString());
};
template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Value::set(T name, qjs::Value value) {
  return this->set(name.toStdString(), value);
};

template <typename V, typename T,
          std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Value::set(T name, V value) {
  return this->set(name.toStdString(), value);
};
template <typename T, std::enable_if_t<is_qtstring_or_derived<T>, int>>
bool Value::drop(T name) {
  return this->drop(name.toStdString());
};

#endif

} // namespace qjs