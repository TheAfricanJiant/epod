/*
 * Merge shim: person detection model -> this library's TensorFlow Lite Micro.
 * ---------------------------------------------------------------------------
 * The two Edge Impulse projects were exported months apart and against
 * different TFLite Micro generations, so the person model asks for one operator
 * by its old name and signature:
 *
 *     tflite::ops::micro::Register_TFLite_Detection_PostProcess()   // by value
 *
 * while the SDK shipped in this library provides the current form:
 *
 *     tflite::Register_DETECTION_POSTPROCESS()                      // pointer
 *
 * Same kernel, same registration struct - only the namespace and the return
 * convention changed. This forwards one to the other rather than pinning the
 * whole library to an older SDK, which would have meant regressing the voice
 * model too.
 *
 * If a future re-export of either project stops linking on some other
 * Register_* symbol, that is the same story again and belongs here beside this
 * one.
 */

#include "edge-impulse-sdk/tensorflow/lite/micro/micro_mutable_op_resolver.h"

namespace tflite {
namespace ops {
namespace micro {

TfLiteRegistration Register_TFLite_Detection_PostProcess(void) {
  // The SDK owns the registration; it is a function-local static with static
  // storage duration, so dereferencing and copying it here is safe and the
  // copy stays valid for the life of the interpreter.
  return *::tflite::Register_DETECTION_POSTPROCESS();
}

}  // namespace micro
}  // namespace ops
}  // namespace tflite
