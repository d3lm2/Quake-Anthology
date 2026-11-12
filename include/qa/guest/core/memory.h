/*
 * Port of src/guest/core/memory.ts: SparseGuestMemory, GuestMemoryFault
 * (see contracts.h throw channel) and the guest-pointer helpers.
 * C accommodations: the class becomes qa_sparse_guest_memory embedding
 * the registers.h qa_mapped_guest_memory vtable as `base` (CPUs and the
 * numeric leaves consume &mem->base). TS closures become small structs
 * (fetch sequence, executable guards, write-observer handles). GuestAddress has no
 * addressSpace token in C, so foreign-address-space faults cannot be
 * detected (registers.h precedent). An observer callback that raises
 * is collected as TS does: the other observers still run, one error is
 * rethrown as it was and several become an AggregateError.
 * Ownership: TS frees a backing buffer when its last mapping and
 * observer are gone; here backings are reference counted and retired
 * mappings are freed at once (guards and fetch sequences revalidate by
 * generation and mapping serial instead of holding the mapping).
 * GPL-2.0-or-later.
 */
#ifndef QA_GUEST_CORE_MEMORY_H
#define QA_GUEST_CORE_MEMORY_H

#include "qa/guest/core/contracts.h"

/* BigInt.asUintN/asIntN(pointerBytes*8, ...) helpers. */
uint64_t qa_wrap_guest_pointer(uint64_t value, int pointer_bytes);
int64_t qa_signed_guest_pointer(uint64_t value, int pointer_bytes);
uint64_t qa_add_guest_pointer(uint64_t value, int64_t displacement, int pointer_bytes);

typedef struct qa_sparse_backing {
  uint8_t *bytes;
  size_t byte_length;
  bool exposed; /* a borrow() view of this buffer exists */
  uint64_t *page_revisions; /* lazily allocated, one per 4096 bytes */
  size_t page_count;
  size_t refs;           /* mappings and observer chunks that hold this buffer */
  size_t slot;           /* index in the owner's backing list */
  size_t snapshot_index; /* scratch for checkpoint */
} qa_sparse_backing;

typedef struct qa_sparse_mapping {
  uint64_t base;
  size_t byte_length;
  qa_guest_permissions permissions;
  const char *label; /* interned in the memory: stays valid until the memory is destroyed */
  qa_sparse_backing *backing;
  size_t backing_offset; /* TS bytes.byteOffset */
  uint64_t end;          /* base + byte_length (<= 2^64, may wrap to 0 only at exactly 2^64) */
  bool end_wraps;        /* end == 2^64 */
  bool active;
  uint64_t serial;       /* unique per memory: identity of the TS mapping object */
} qa_sparse_mapping;

typedef struct qa_sparse_write_observer qa_sparse_write_observer;
typedef uint64_t qa_guest_write_observer_handle; /* 0 = none */
typedef void (*qa_guest_write_observer_fn)(void *ctx, const qa_guest_written_range *ranges, size_t range_count);

#define QA_SPARSE_WORKING_SET 8u /* power of two */

typedef struct qa_sparse_guest_memory {
  qa_mapped_guest_memory base;
  qa_guest_module_ref module;
  bool managed;
  /* Installed by a CPU run; NULL means faults abort (uncaught throw). */
  qa_guest_throw *throw_ctx;
  /* private */
  uint64_t allocation_base;
  qa_sparse_mapping **mappings; /* sorted, disjoint */
  size_t mapping_count, mapping_cap;
  uint64_t next_mapping_serial;
  /* Mapping labels, one copy per distinct text (open-addressing set). qa_sparse_mappings hands these pointers
   * out, and its callers keep them past the mapping's own life as TS keeps the label strings. */
  char **label_slots;
  size_t label_capacity, label_count;
  size_t notify_depth;      /* observer deliveries in progress */
  bool observers_unlinked;  /* a removal was deferred until deliveries finish */
  qa_sparse_backing **backings;
  size_t backing_count, backing_cap;
  qa_sparse_write_observer *observers; /* list head via impl in .c (opaque node) */
  uint64_t next_observer_id;
  uint64_t generation;
  struct qa_sparse_hint { uint64_t alignment, base; size_t byte_length; } *hints;
  size_t hint_count, hint_cap;
  /* TS #slabs / #freePages: pooled pages, 64 per slab. */
  struct qa_sparse_slab { uint64_t base; uint8_t used[64]; size_t next; } *slabs;
  size_t slab_count, slab_cap;
  size_t *free_pages;
  size_t free_page_count, free_page_cap;
  /* TS #recentMappings (read, write, execute, any) and #workingSet. */
  qa_sparse_mapping *recent_mappings[4];
  qa_sparse_mapping *working_set[QA_SPARSE_WORKING_SET];
  size_t next_working_mapping;
  /* TS #writeDeliveries: stores that reached at least one observer. */
  uint64_t write_deliveries;
  uint8_t dummy;
} qa_sparse_guest_memory;

typedef struct qa_sparse_guest_memory_options {
  qa_guest_module_ref module;
  int pointer_bytes; /* 4 | 8 */
  uint64_t allocation_base;
  bool has_allocation_base; /* false = TS undefined (0x10000) */
} qa_sparse_guest_memory_options;

void qa_sparse_guest_memory_init(qa_sparse_guest_memory *mem, const qa_sparse_guest_memory_options *options);
void qa_sparse_guest_memory_init_managed(qa_sparse_guest_memory *mem, const qa_sparse_guest_memory_options *options);
void qa_sparse_guest_memory_destroy(qa_sparse_guest_memory *mem);
bool qa_sparse_guest_memory_managed(const qa_sparse_guest_memory *mem);
bool qa_sparse_has_write_observers(const qa_sparse_guest_memory *mem);
/* TS writeDeliveries: counts stores that reached at least one observer. */
uint64_t qa_sparse_write_deliveries(const qa_sparse_guest_memory *mem);

bool qa_sparse_pointer(qa_sparse_guest_memory *mem, uint64_t raw_value, qa_guest_core_address *out);
qa_guest_core_address qa_sparse_offset(qa_sparse_guest_memory *mem, qa_guest_core_address address, int64_t displacement);
qa_guest_core_address qa_sparse_map(qa_sparse_guest_memory *mem, const qa_guest_map_options *options);
qa_guest_core_address qa_sparse_allocate(qa_sparse_guest_memory *mem, const qa_guest_allocation_options *options);
qa_guest_core_address qa_sparse_map_alias(qa_sparse_guest_memory *mem, uint64_t base, size_t byte_length,
                                     qa_guest_permissions permissions, const char *label, qa_guest_core_address source);
void qa_sparse_unmap(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length);
void qa_sparse_protect(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, qa_guest_permissions permissions);
/* Returns the mapping count; writes up to `capacity` entries. */
size_t qa_sparse_mappings(const qa_sparse_guest_memory *mem, qa_guest_mapping *out, size_t capacity);
void qa_sparse_check(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, qa_guest_access access);
/* Live host view (TS DataView); marks the backing exposed. */
uint8_t *qa_sparse_borrow(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length);
void qa_sparse_copy(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest, size_t byte_length);
void qa_sparse_copy_into(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest,
                         size_t dest_length, size_t dest_offset, size_t byte_length);
int64_t qa_sparse_find_zero(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t maximum);
void qa_sparse_fetch(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint8_t *dest, size_t byte_length);
uint8_t qa_sparse_fetch_byte(qa_sparse_guest_memory *mem, uint64_t byte_offset);
void qa_sparse_write(qa_sparse_guest_memory *mem, qa_guest_core_address address, const uint8_t *bytes, size_t byte_length);
void qa_sparse_move(qa_sparse_guest_memory *mem, qa_guest_core_address destination, qa_guest_core_address source, size_t byte_length);
void qa_sparse_fill(qa_sparse_guest_memory *mem, qa_guest_core_address address, size_t byte_length, uint32_t value);
qa_guest_write_observer_handle qa_sparse_observe_writes(qa_sparse_guest_memory *mem, qa_guest_core_address address,
                                                        size_t byte_length, qa_guest_write_observer_fn notify, void *ctx);
void qa_sparse_unobserve_writes(qa_sparse_guest_memory *mem, qa_guest_write_observer_handle handle);

/* Scalars (all little-endian; float reads widen to double as JS numbers do). */
uint8_t qa_sparse_read_u8(qa_sparse_guest_memory *mem, qa_guest_core_address address);
int8_t qa_sparse_read_i8(qa_sparse_guest_memory *mem, qa_guest_core_address address);
uint16_t qa_sparse_read_u16(qa_sparse_guest_memory *mem, qa_guest_core_address address);
int16_t qa_sparse_read_i16(qa_sparse_guest_memory *mem, qa_guest_core_address address);
uint32_t qa_sparse_read_u32(qa_sparse_guest_memory *mem, qa_guest_core_address address);
int32_t qa_sparse_read_i32(qa_sparse_guest_memory *mem, qa_guest_core_address address);
uint64_t qa_sparse_read_u64(qa_sparse_guest_memory *mem, qa_guest_core_address address);
int64_t qa_sparse_read_i64(qa_sparse_guest_memory *mem, qa_guest_core_address address);
void qa_sparse_read_u64_words(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t *low, uint32_t *high);
double qa_sparse_read_f32(qa_sparse_guest_memory *mem, qa_guest_core_address address);
qa_vec3 qa_sparse_read_f32_vector(qa_sparse_guest_memory *mem, qa_guest_core_address address);
double qa_sparse_read_f64(qa_sparse_guest_memory *mem, qa_guest_core_address address);
bool qa_sparse_read_pointer(qa_sparse_guest_memory *mem, qa_guest_core_address address, qa_guest_core_address *out);
void qa_sparse_write_u8(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t value);
void qa_sparse_write_i8(qa_sparse_guest_memory *mem, qa_guest_core_address address, int32_t value);
void qa_sparse_write_u16(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t value);
void qa_sparse_write_i16(qa_sparse_guest_memory *mem, qa_guest_core_address address, int32_t value);
void qa_sparse_write_u32(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t value);
void qa_sparse_write_i32(qa_sparse_guest_memory *mem, qa_guest_core_address address, int32_t value);
void qa_sparse_write_u64(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint64_t value);
void qa_sparse_write_i64(qa_sparse_guest_memory *mem, qa_guest_core_address address, int64_t value);
void qa_sparse_write_u64_words(qa_sparse_guest_memory *mem, qa_guest_core_address address, uint32_t low, uint32_t high);
void qa_sparse_write_f32(qa_sparse_guest_memory *mem, qa_guest_core_address address, double value);
void qa_sparse_write_f64(qa_sparse_guest_memory *mem, qa_guest_core_address address, double value);
void qa_sparse_write_pointer(qa_sparse_guest_memory *mem, qa_guest_core_address address, bool has_value, qa_guest_core_address value);

/* TS scalarRead: number-addressed read for interpreters; -1 when the access is
 * not inside a recently used readable mapping (nothing changes on a miss). */
int64_t qa_sparse_scalar_read(qa_sparse_guest_memory *mem, uint64_t address, int byte_length);
/* TS scalarWrite: false means nothing was written and the checked path must run. */
bool qa_sparse_scalar_write(qa_sparse_guest_memory *mem, uint64_t address, int byte_length, uint32_t value);

/* TS allocatorImage: contracts/execution.h qa_guest_allocator_image with
 * malloc'd arrays; release with qa_guest_allocator_image_free. */
void qa_sparse_allocator_image(const qa_sparse_guest_memory *mem, qa_guest_allocator_image *out);
void qa_guest_allocator_image_free(qa_guest_allocator_image *image);
/* TS restoreAllocator: replaces the allocation cursor and pool state; the
 * mappings they describe must already exist. Invalid images raise RangeError. */
void qa_sparse_restore_allocator(qa_sparse_guest_memory *mem, const qa_guest_allocator_image *image);

/* TS fetchSequence closure. */
typedef struct qa_guest_fetch_sequence {
  qa_sparse_guest_memory *mem;
  uint64_t start;
  uint64_t consumed;
  uint64_t generation;
  qa_sparse_mapping *mapping;
  size_t offset;
  bool started;
} qa_guest_fetch_sequence;
void qa_sparse_fetch_sequence_init(qa_guest_fetch_sequence *seq, qa_sparse_guest_memory *mem, uint64_t byte_offset);
uint8_t qa_guest_fetch_sequence_next(qa_guest_fetch_sequence *seq);

/* TS retainExecutableBytes/Range/Block guards. */
typedef struct qa_guest_executable_guard {
  bool valid; /* false = TS null */
  qa_sparse_guest_memory *mem;
  qa_sparse_mapping *mapping; /* valid only while mapping_serial is still mapped */
  uint64_t address;           /* first guarded guest byte */
  uint64_t mapping_serial;
  uint64_t generation;        /* memory generation at the last liveness check */
  size_t mapping_offset;
  uint8_t *expected; /* owned */
  size_t byte_length;
  bool has_page; /* single-page revision fast path */
  size_t page;
  bool has_checked;
  uint64_t checked;
  /* block pages (retainExecutableBlock) */
  size_t *block_pages;
  uint64_t *block_checked;
  size_t block_page_count;
} qa_guest_executable_guard;
bool qa_sparse_retain_executable_bytes(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out);
bool qa_sparse_retain_executable_range(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out);
bool qa_sparse_retain_executable_block(qa_sparse_guest_memory *mem, uint64_t byte_offset,
                                       const uint8_t *bytes, size_t byte_length, qa_guest_executable_guard *out);
bool qa_guest_executable_guard_unchanged(qa_guest_executable_guard *guard);
bool qa_guest_executable_block_after_store(const qa_guest_executable_guard *guard);
void qa_guest_executable_guard_free(qa_guest_executable_guard *guard);

/* False only on OOM. */
bool qa_sparse_checkpoint(qa_sparse_guest_memory *mem, qa_guest_memory_snapshot *out);
bool qa_sparse_guest_memory_restore(qa_sparse_guest_memory *out, const qa_guest_module_ref *module,
                                    const qa_guest_memory_snapshot *snapshot);

#endif
