#ifndef RefalRTS_DYNAMIC_H_
#define RefalRTS_DYNAMIC_H_

#include <map>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "refalrts.h"


//==============================================================================
// Динамическое связывание
//==============================================================================

namespace refalrts {

// Использовать class, public и private нельзя — требуется статическая
// инициализация:
//   X x = { ... };
struct Module {
  IdentReference *list_idents;
  unsigned int next_ident_id;
  ExternalReference *list_externals;
  unsigned int next_external_id;
  size_t global_variables_memory;
};

struct StringRef {
  explicit StringRef(const char *str = "")
    : str(str ? str : "")
  {
    /* пусто */
  }

  // Конструктор копирования создаётся компилятором
  // Оператор присваивания создаётся компилятором

  bool operator<(const StringRef& other) const {
    return strcmp(str, other.str) < 0;
  }

  const char *str;
};

inline bool operator<(const RefalFuncName& lhs, const RefalFuncName& rhs) {
  if (lhs.cookie1 < rhs.cookie1) {
    return true;
  } else if (lhs.cookie1 > rhs.cookie1) {
    return false;
  } else if (lhs.cookie2 < rhs.cookie2) {
    return true;
  } else if (lhs.cookie2 > rhs.cookie2) {
    return false;
  } else {
    return strcmp(lhs.name, rhs.name) < 0;
  }
}

class Dynamic {
public:
  static UInt32 one_at_a_time(UInt32 init, const char *bytes, size_t length);

  template <typename Key>
  struct HashKeyTraits {
    /* ничего */
  };

  template <typename Key, typename Value>
  class DynamicHash {
    // Копирование запрещено
    DynamicHash(const DynamicHash&);
    DynamicHash& operator=(const DynamicHash&);
  public:
    DynamicHash();
    ~DynamicHash();

    size_t count() const {
      return m_count;
    }

    Value *alloc(Key key);
    Value *lookup(Key key);

  private:
    struct Node {
      UInt32 hash;
      Value value;
      typename DynamicHash<Key, Value>::Node *next;

      Node(UInt32 hash, Node *next)
        : hash(hash)
        , value()
        , next(next)
      {
        /* пусто */
      }
    };

    typedef typename DynamicHash<Key, Value>::Node *NodePtr;
    enum { cResizeThreshold = 4 };

    void rehash();

    Node **m_table;
    unsigned int m_table_power;
    size_t m_count;
  };

private:
  struct ConstTable {
    UInt32 cookie1;
    UInt32 cookie2;
    FunctionTableItem *externals;
    FunctionTable *function_table;
    RefalIdentifier *idents;
    RefalNumber *numbers;
    StringItem *strings;
    RASLCommand *rasl;

    char *external_memory;
    char *idents_memory;
    char *strings_memory;

    ConstTable *next;

    RefalFuncName make_name(const char *name) const;
  };

  struct AtExitListNode {
    AtExitCB callback;
    void *data;
    AtExitListNode *next;

    AtExitListNode(AtExitCB callback, void *data, Dynamic *dynamic)
      : callback(callback), data(data), next(dynamic->m_at_exit_list)
    {
      dynamic->m_at_exit_list = this;
    }

    void call(Dynamic *dynamic) {
      callback(dynamic, data);
    }
  };

  friend struct AtExitListNode;

  typedef std::map<RefalFuncName, RefalFunction*> FuncsMap;
  typedef std::map<StringRef, RefalIdentifier> IdentsMap;

  struct FunctionTable *m_unresolved_func_tables;
  FuncsMap m_funcs_table;
  struct ConstTable *m_tables;
  IdentsMap m_idents_table;
  RefalIdentifier *m_native_identifiers;
  RefalFunction **m_native_externals;
  Module *m_main_module;
  AtExitListNode *m_at_exit_list;
  char *m_global_variables;

public:
  Dynamic(Module *main_module);

  size_t idents_count();

  void free_idents_table();

  RefalIdentifier lookup_ident(const char *name);
  bool register_ident(RefalIdentifier ident);

  void load_native_identifiers();
  RefalIdentifier operator[](const IdentReference& ref) const {
    return m_native_identifiers[ref.id];
  }

  RefalFunction *lookup_function(const RefalFuncName& name);
  bool register_function(RefalFunction *func);

  unsigned find_unresolved_externals();
  void free_funcs_table();

  RefalFunction* operator[](const ExternalReference& ref) const {
    return m_native_externals[ref.id];
  }

  void register_(FunctionTable *table) {
    table->next = m_unresolved_func_tables;
    m_unresolved_func_tables = table;
  }

  template <typename T>
  static T *malloc(size_t count = 1) {
    T *result = static_cast<T*>(::malloc(sizeof(T) * count));
    assert(count == 0 || result);
    return result;
  }

  void enumerate_blocks();
  void cleanup_module();

  bool seek_rasl_signature(FILE *stream);
  const char *read_asciiz(FILE *stream);
  void read_counters(unsigned long counters[]);

  void at_exit(AtExitCB callback, void *data);
  void perform_at_exit();

  void alloc_global_variables();
  void free_global_variables();
  void *global_variable(size_t offset) {
    return &m_global_variables[offset];
  }
};

}  // namespace refalrts


//------------------------------------------------------------------------------
// Хеш-таблица
//------------------------------------------------------------------------------

template <typename Key, typename Value>
refalrts::Dynamic::DynamicHash<Key, Value>::DynamicHash()
  : m_table()
  , m_table_power(5)
  , m_count(0)
{
  size_t table_size = size_t(1) << m_table_power;
  m_table = new DynamicHash<Key, Value>::NodePtr[table_size];

  for (size_t i = 0; i < table_size; ++i) {
    m_table[i] = 0;
  }
}

template <typename Key, typename Value>
refalrts::Dynamic::DynamicHash<Key, Value>::~DynamicHash() {
  size_t table_size = size_t(1) << m_table_power;
  for (size_t i = 0; i < table_size; ++i) {
    Node *node = m_table[i];
    while (node != 0) {
      node->value.cleanup();
      Node *next = node->next;
      delete node;
      node = next;
    }
  }

  delete[] m_table;
  m_table = 0;
}

template <typename Key, typename Value>
Value * refalrts::Dynamic::DynamicHash<Key, Value>::alloc(Key key) {
  // Хаки для Watcom
  using refalrts::UInt32;

  rehash();

  UInt32 hash = HashKeyTraits<Key>::hash(key);
  UInt32 hash_mask = (UInt32(1) << m_table_power) - 1;

  Node **pstart_node = &m_table[hash & hash_mask];
  Node *return_node = *pstart_node;
  while (
    return_node != 0
    && (
      return_node->hash != hash
      || ! HashKeyTraits<Key>::equal(return_node->value.key(), key)
    )
  ) {
    return_node = return_node->next;
  }

  if (return_node == 0) {
    return_node = new Node(hash, *pstart_node);
    *pstart_node = return_node;
    ++m_count;
  }

  return &return_node->value;
}

template <typename Key, typename Value>
Value *refalrts::Dynamic::DynamicHash<Key, Value>::lookup(Key key) {
  // Хаки для Watcom
  using refalrts::UInt32;

  UInt32 hash = HashKeyTraits<Key>::hash(key);
  UInt32 hash_mask = (UInt32(1) << m_table_power) - 1;

  Node *return_node = m_table[hash & hash_mask];
  while (
    return_node != 0
    && (
      return_node->hash != hash
      || ! HashKeyTraits<Key>::equal(return_node->value.key(), key)
    )
  ) {
    return_node = return_node->next;
  }

  if (return_node != 0) {
    return &return_node->value;
  } else {
    return 0;
  }
}

template <typename Key, typename Value>
void refalrts::Dynamic::DynamicHash<Key, Value>::rehash() {
  // Хаки для Watcom
  using refalrts::UInt32;

  size_t table_size = size_t(1) << m_table_power;

  if (m_count / table_size < cResizeThreshold) {
    return;
  }

  unsigned int new_table_power = m_table_power + 1;
  size_t new_table_size = size_t(1) << new_table_power;
  UInt32 hash_mask = (UInt32(1) << new_table_power) - 1;
  Node **new_table = new DynamicHash<Key, Value>::NodePtr[new_table_size];

  for (size_t i = 0; i < new_table_size; ++i) {
    new_table[i] = 0;
  }

  for (size_t i = 0; i < table_size; ++i) {
    Node *node = m_table[i];
    while (node != 0) {
      Node *next_in_old_table = node->next;
      Node **pnext_in_new_table = &new_table[node->hash & hash_mask];
      node->next = *pnext_in_new_table;
      *pnext_in_new_table = node;
      node = next_in_old_table;
    }
  }

  m_table_power = new_table_power;
  m_table = new_table;
}

#endif  // RefalRTS_DYNAMIC_H_