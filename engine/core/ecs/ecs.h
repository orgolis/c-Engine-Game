#pragma once

// ECS module aggregator (Master Plan Stage 1). The data-oriented world
// (EnTT-backed), core POD components (reflected), and job-parallel iteration.
#include "ecs/world.h"
#include "ecs/components.h"
#include "ecs/parallel.h"
#include "ecs/system_scheduler.h"
#include "ecs/transform_systems.h"   // scheduled transform -> LocalToWorld (1.3)
#include "ecs/render_collect.h"      // single-pass ECS draw submission (1.4)
#include "ecs/snapshot.h"            // world snapshot save/load (0.5 step 5)
