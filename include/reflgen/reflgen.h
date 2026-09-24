#pragma once
// reflgen 공개 관문 — 리플렉션 코어, 런타임 서술자·등록소, 직렬화.
// 포맷 백엔드는 따로 포함한다: reflgen/json.h, reflgen/binary.h.
#include "reflgen/core/attributes.h"
#include "reflgen/core/descriptor.h"
#include "reflgen/core/enum.h"
#include "reflgen/core/hook.h"
#include "reflgen/core/name.h"
#include "reflgen/core/schema.h"
#include "reflgen/core/type_id.h"
#include "reflgen/runtime/registry.h"
#include "reflgen/runtime/type_descriptor.h"
#include "reflgen/serial/container_traits.h"
#include "reflgen/serial/error.h"
#include "reflgen/serial/reader.h"
#include "reflgen/serial/serializer.h"
#include "reflgen/serial/writer.h"
