#pragma once

#pragma warning(push)
#pragma warning(disable: 4996)
#include "./vstsdk2.4/pluginterfaces/vst2.x/aeffectx.h"
#pragma warning(pop)

namespace hwm {

struct VstPlugin;

struct HostApplication
{
	HostApplication(
		size_t sampling_rate,
		size_t block_size );

	VstIntPtr Callback(VstPlugin* vst, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);

private:
	size_t sampling_rate_;
	size_t block_size_;
	VstTimeInfo	timeinfo_;
};

//! プラグイン内部からの要求などを受けて呼び出される
//! Host側のコールバック関数
VstIntPtr VSTCALLBACK VstHostCallback(AEffect* effect, VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt);

}	//::hwm
