// generated from rosidl_generator_c/resource/idl__functions.c.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice
#include "techx_vision_bridge/msg/detail/target3_d__functions.h"

#include <assert.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#include "rcutils/allocator.h"


// Include directives for member types
// Member `header`
#include "std_msgs/msg/detail/header__functions.h"

bool
techx_vision_bridge__msg__Target3D__init(techx_vision_bridge__msg__Target3D * msg)
{
  if (!msg) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__init(&msg->header)) {
    techx_vision_bridge__msg__Target3D__fini(msg);
    return false;
  }
  // track_id
  // x
  // y
  // z
  return true;
}

void
techx_vision_bridge__msg__Target3D__fini(techx_vision_bridge__msg__Target3D * msg)
{
  if (!msg) {
    return;
  }
  // header
  std_msgs__msg__Header__fini(&msg->header);
  // track_id
  // x
  // y
  // z
}

bool
techx_vision_bridge__msg__Target3D__are_equal(const techx_vision_bridge__msg__Target3D * lhs, const techx_vision_bridge__msg__Target3D * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__are_equal(
      &(lhs->header), &(rhs->header)))
  {
    return false;
  }
  // track_id
  if (lhs->track_id != rhs->track_id) {
    return false;
  }
  // x
  if (lhs->x != rhs->x) {
    return false;
  }
  // y
  if (lhs->y != rhs->y) {
    return false;
  }
  // z
  if (lhs->z != rhs->z) {
    return false;
  }
  return true;
}

bool
techx_vision_bridge__msg__Target3D__copy(
  const techx_vision_bridge__msg__Target3D * input,
  techx_vision_bridge__msg__Target3D * output)
{
  if (!input || !output) {
    return false;
  }
  // header
  if (!std_msgs__msg__Header__copy(
      &(input->header), &(output->header)))
  {
    return false;
  }
  // track_id
  output->track_id = input->track_id;
  // x
  output->x = input->x;
  // y
  output->y = input->y;
  // z
  output->z = input->z;
  return true;
}

techx_vision_bridge__msg__Target3D *
techx_vision_bridge__msg__Target3D__create()
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  techx_vision_bridge__msg__Target3D * msg = (techx_vision_bridge__msg__Target3D *)allocator.allocate(sizeof(techx_vision_bridge__msg__Target3D), allocator.state);
  if (!msg) {
    return NULL;
  }
  memset(msg, 0, sizeof(techx_vision_bridge__msg__Target3D));
  bool success = techx_vision_bridge__msg__Target3D__init(msg);
  if (!success) {
    allocator.deallocate(msg, allocator.state);
    return NULL;
  }
  return msg;
}

void
techx_vision_bridge__msg__Target3D__destroy(techx_vision_bridge__msg__Target3D * msg)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (msg) {
    techx_vision_bridge__msg__Target3D__fini(msg);
  }
  allocator.deallocate(msg, allocator.state);
}


bool
techx_vision_bridge__msg__Target3D__Sequence__init(techx_vision_bridge__msg__Target3D__Sequence * array, size_t size)
{
  if (!array) {
    return false;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  techx_vision_bridge__msg__Target3D * data = NULL;

  if (size) {
    data = (techx_vision_bridge__msg__Target3D *)allocator.zero_allocate(size, sizeof(techx_vision_bridge__msg__Target3D), allocator.state);
    if (!data) {
      return false;
    }
    // initialize all array elements
    size_t i;
    for (i = 0; i < size; ++i) {
      bool success = techx_vision_bridge__msg__Target3D__init(&data[i]);
      if (!success) {
        break;
      }
    }
    if (i < size) {
      // if initialization failed finalize the already initialized array elements
      for (; i > 0; --i) {
        techx_vision_bridge__msg__Target3D__fini(&data[i - 1]);
      }
      allocator.deallocate(data, allocator.state);
      return false;
    }
  }
  array->data = data;
  array->size = size;
  array->capacity = size;
  return true;
}

void
techx_vision_bridge__msg__Target3D__Sequence__fini(techx_vision_bridge__msg__Target3D__Sequence * array)
{
  if (!array) {
    return;
  }
  rcutils_allocator_t allocator = rcutils_get_default_allocator();

  if (array->data) {
    // ensure that data and capacity values are consistent
    assert(array->capacity > 0);
    // finalize all array elements
    for (size_t i = 0; i < array->capacity; ++i) {
      techx_vision_bridge__msg__Target3D__fini(&array->data[i]);
    }
    allocator.deallocate(array->data, allocator.state);
    array->data = NULL;
    array->size = 0;
    array->capacity = 0;
  } else {
    // ensure that data, size, and capacity values are consistent
    assert(0 == array->size);
    assert(0 == array->capacity);
  }
}

techx_vision_bridge__msg__Target3D__Sequence *
techx_vision_bridge__msg__Target3D__Sequence__create(size_t size)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  techx_vision_bridge__msg__Target3D__Sequence * array = (techx_vision_bridge__msg__Target3D__Sequence *)allocator.allocate(sizeof(techx_vision_bridge__msg__Target3D__Sequence), allocator.state);
  if (!array) {
    return NULL;
  }
  bool success = techx_vision_bridge__msg__Target3D__Sequence__init(array, size);
  if (!success) {
    allocator.deallocate(array, allocator.state);
    return NULL;
  }
  return array;
}

void
techx_vision_bridge__msg__Target3D__Sequence__destroy(techx_vision_bridge__msg__Target3D__Sequence * array)
{
  rcutils_allocator_t allocator = rcutils_get_default_allocator();
  if (array) {
    techx_vision_bridge__msg__Target3D__Sequence__fini(array);
  }
  allocator.deallocate(array, allocator.state);
}

bool
techx_vision_bridge__msg__Target3D__Sequence__are_equal(const techx_vision_bridge__msg__Target3D__Sequence * lhs, const techx_vision_bridge__msg__Target3D__Sequence * rhs)
{
  if (!lhs || !rhs) {
    return false;
  }
  if (lhs->size != rhs->size) {
    return false;
  }
  for (size_t i = 0; i < lhs->size; ++i) {
    if (!techx_vision_bridge__msg__Target3D__are_equal(&(lhs->data[i]), &(rhs->data[i]))) {
      return false;
    }
  }
  return true;
}

bool
techx_vision_bridge__msg__Target3D__Sequence__copy(
  const techx_vision_bridge__msg__Target3D__Sequence * input,
  techx_vision_bridge__msg__Target3D__Sequence * output)
{
  if (!input || !output) {
    return false;
  }
  if (output->capacity < input->size) {
    const size_t allocation_size =
      input->size * sizeof(techx_vision_bridge__msg__Target3D);
    rcutils_allocator_t allocator = rcutils_get_default_allocator();
    techx_vision_bridge__msg__Target3D * data =
      (techx_vision_bridge__msg__Target3D *)allocator.reallocate(
      output->data, allocation_size, allocator.state);
    if (!data) {
      return false;
    }
    // If reallocation succeeded, memory may or may not have been moved
    // to fulfill the allocation request, invalidating output->data.
    output->data = data;
    for (size_t i = output->capacity; i < input->size; ++i) {
      if (!techx_vision_bridge__msg__Target3D__init(&output->data[i])) {
        // If initialization of any new item fails, roll back
        // all previously initialized items. Existing items
        // in output are to be left unmodified.
        for (; i-- > output->capacity; ) {
          techx_vision_bridge__msg__Target3D__fini(&output->data[i]);
        }
        return false;
      }
    }
    output->capacity = input->size;
  }
  output->size = input->size;
  for (size_t i = 0; i < input->size; ++i) {
    if (!techx_vision_bridge__msg__Target3D__copy(
        &(input->data[i]), &(output->data[i])))
    {
      return false;
    }
  }
  return true;
}
