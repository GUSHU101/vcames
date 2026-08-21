#include "yuv420_converter.h"

#include <jni.h>

#include <cstddef>
#include <cstdint>
#include <string>

namespace {

vcames::YuvPlaneView Plane(
        JNIEnv* env,
        jobject buffer,
        jint limit,
        jint position,
        jint row_stride,
        jint pixel_stride) {
    vcames::YuvPlaneView plane;
    plane.data = static_cast<const uint8_t*>(env->GetDirectBufferAddress(buffer));
    plane.limit = limit < 0 ? 0 : static_cast<size_t>(limit);
    plane.position = position < 0 ? plane.limit : static_cast<size_t>(position);
    plane.row_stride = row_stride <= 0 ? 0 : static_cast<size_t>(row_stride);
    plane.pixel_stride = pixel_stride <= 0 ? 0 : static_cast<size_t>(pixel_stride);
    const jlong capacity = env->GetDirectBufferCapacity(buffer);
    if (capacity < 0 || plane.limit > static_cast<size_t>(capacity)) {
        plane.data = nullptr;
    }
    return plane;
}

}  // namespace

extern "C" JNIEXPORT jboolean JNICALL
Java_io_github_gushu101_vcames_Yuv420Converter_nativeConvert(
        JNIEnv* env,
        jclass,
        jobject y_buffer,
        jint y_limit,
        jint y_position,
        jint y_row_stride,
        jint y_pixel_stride,
        jobject u_buffer,
        jint u_limit,
        jint u_position,
        jint u_row_stride,
        jint u_pixel_stride,
        jobject v_buffer,
        jint v_limit,
        jint v_position,
        jint v_row_stride,
        jint v_pixel_stride,
        jint crop_left,
        jint crop_top,
        jint width,
        jint height,
        jbyteArray output) {
    if (output == nullptr || crop_left < 0 || crop_top < 0
            || width <= 0 || height <= 0) {
        return JNI_FALSE;
    }
    const vcames::YuvPlaneView y = Plane(
            env, y_buffer, y_limit, y_position, y_row_stride, y_pixel_stride);
    const vcames::YuvPlaneView u = Plane(
            env, u_buffer, u_limit, u_position, u_row_stride, u_pixel_stride);
    const vcames::YuvPlaneView v = Plane(
            env, v_buffer, v_limit, v_position, v_row_stride, v_pixel_stride);
    if (y.data == nullptr || u.data == nullptr || v.data == nullptr) {
        return JNI_FALSE;
    }
    const jsize output_size = env->GetArrayLength(output);
    auto* output_bytes = static_cast<uint8_t*>(
            env->GetPrimitiveArrayCritical(output, nullptr));
    if (output_bytes == nullptr) {
        return JNI_FALSE;
    }
    std::string error;
    const bool converted = vcames::ConvertYuv420ToNv21(
            y, u, v,
            static_cast<uint32_t>(crop_left),
            static_cast<uint32_t>(crop_top),
            static_cast<uint32_t>(width),
            static_cast<uint32_t>(height),
            output_bytes,
            static_cast<size_t>(output_size),
            &error);
    env->ReleasePrimitiveArrayCritical(output, output_bytes, 0);
    return converted ? JNI_TRUE : JNI_FALSE;
}
