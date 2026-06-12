// generated from rosidl_generator_c/resource/idl__functions.h.em
// with input from techx_vision_bridge:msg/Target3D.idl
// generated code does not contain a copyright notice

#ifndef TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__FUNCTIONS_H_
#define TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__FUNCTIONS_H_

#ifdef __cplusplus
extern "C"
{
#endif

#include <stdbool.h>
#include <stdlib.h>

#include "rosidl_runtime_c/visibility_control.h"
#include "techx_vision_bridge/msg/rosidl_generator_c__visibility_control.h"

#include "techx_vision_bridge/msg/detail/target3_d__struct.h"

/// Initialize msg/Target3D message.
/**
 * If the init function is called twice for the same message without
 * calling fini inbetween previously allocated memory will be leaked.
 * \param[in,out] msg The previously allocated message pointer.
 * Fields without a default value will not be initialized by this function.
 * You might want to call memset(msg, 0, sizeof(
 * techx_vision_bridge__msg__Target3D
 * )) before or use
 * techx_vision_bridge__msg__Target3D__create()
 * to allocate and initialize the message.
 * \return true if initialization was successful, otherwise false
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__init(techx_vision_bridge__msg__Target3D * msg);

/// Finalize msg/Target3D message.
/**
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
void
techx_vision_bridge__msg__Target3D__fini(techx_vision_bridge__msg__Target3D * msg);

/// Create msg/Target3D message.
/**
 * It allocates the memory for the message, sets the memory to zero, and
 * calls
 * techx_vision_bridge__msg__Target3D__init().
 * \return The pointer to the initialized message if successful,
 * otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
techx_vision_bridge__msg__Target3D *
techx_vision_bridge__msg__Target3D__create();

/// Destroy msg/Target3D message.
/**
 * It calls
 * techx_vision_bridge__msg__Target3D__fini()
 * and frees the memory of the message.
 * \param[in,out] msg The allocated message pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
void
techx_vision_bridge__msg__Target3D__destroy(techx_vision_bridge__msg__Target3D * msg);

/// Check for msg/Target3D message equality.
/**
 * \param[in] lhs The message on the left hand size of the equality operator.
 * \param[in] rhs The message on the right hand size of the equality operator.
 * \return true if messages are equal, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__are_equal(const techx_vision_bridge__msg__Target3D * lhs, const techx_vision_bridge__msg__Target3D * rhs);

/// Copy a msg/Target3D message.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source message pointer.
 * \param[out] output The target message pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer is null
 *   or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__copy(
  const techx_vision_bridge__msg__Target3D * input,
  techx_vision_bridge__msg__Target3D * output);

/// Initialize array of msg/Target3D messages.
/**
 * It allocates the memory for the number of elements and calls
 * techx_vision_bridge__msg__Target3D__init()
 * for each element of the array.
 * \param[in,out] array The allocated array pointer.
 * \param[in] size The size / capacity of the array.
 * \return true if initialization was successful, otherwise false
 * If the array pointer is valid and the size is zero it is guaranteed
 # to return true.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__Sequence__init(techx_vision_bridge__msg__Target3D__Sequence * array, size_t size);

/// Finalize array of msg/Target3D messages.
/**
 * It calls
 * techx_vision_bridge__msg__Target3D__fini()
 * for each element of the array and frees the memory for the number of
 * elements.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
void
techx_vision_bridge__msg__Target3D__Sequence__fini(techx_vision_bridge__msg__Target3D__Sequence * array);

/// Create array of msg/Target3D messages.
/**
 * It allocates the memory for the array and calls
 * techx_vision_bridge__msg__Target3D__Sequence__init().
 * \param[in] size The size / capacity of the array.
 * \return The pointer to the initialized array if successful, otherwise NULL
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
techx_vision_bridge__msg__Target3D__Sequence *
techx_vision_bridge__msg__Target3D__Sequence__create(size_t size);

/// Destroy array of msg/Target3D messages.
/**
 * It calls
 * techx_vision_bridge__msg__Target3D__Sequence__fini()
 * on the array,
 * and frees the memory of the array.
 * \param[in,out] array The initialized array pointer.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
void
techx_vision_bridge__msg__Target3D__Sequence__destroy(techx_vision_bridge__msg__Target3D__Sequence * array);

/// Check for msg/Target3D message array equality.
/**
 * \param[in] lhs The message array on the left hand size of the equality operator.
 * \param[in] rhs The message array on the right hand size of the equality operator.
 * \return true if message arrays are equal in size and content, otherwise false.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__Sequence__are_equal(const techx_vision_bridge__msg__Target3D__Sequence * lhs, const techx_vision_bridge__msg__Target3D__Sequence * rhs);

/// Copy an array of msg/Target3D messages.
/**
 * This functions performs a deep copy, as opposed to the shallow copy that
 * plain assignment yields.
 *
 * \param[in] input The source array pointer.
 * \param[out] output The target array pointer, which must
 *   have been initialized before calling this function.
 * \return true if successful, or false if either pointer
 *   is null or memory allocation fails.
 */
ROSIDL_GENERATOR_C_PUBLIC_techx_vision_bridge
bool
techx_vision_bridge__msg__Target3D__Sequence__copy(
  const techx_vision_bridge__msg__Target3D__Sequence * input,
  techx_vision_bridge__msg__Target3D__Sequence * output);

#ifdef __cplusplus
}
#endif

#endif  // TECHX_VISION_BRIDGE__MSG__DETAIL__TARGET3_D__FUNCTIONS_H_
