#pragma once

// Memory management systems
#include "allocator_base.h"
#include "memory_tags.h"
#include "memory_tracker.h"
#include "alloc_worker.h"
#include "stack_allocator.h"
#include "pool_allocator.h"
#include "general_allocator.h"
#include "segregated_allocator.h"
#include "arena_allocator.h"
#include "frame_allocator.h"
#include "locked_allocator.h"

namespace gws::memory {
    // All allocator types are available under gws::memory namespace
}
