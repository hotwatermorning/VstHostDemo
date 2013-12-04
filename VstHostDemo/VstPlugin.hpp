#pragma once

#include <cstdlib>
#include <vector>
#include <stdexcept>
#include <array>

#include <boost/thread/mutex.hpp>
#include <boost/thread/lock_factories.hpp>

#include <windows.h>

#pragma warning(push)
#pragma warning(disable: 4996)
#include "./vstsdk2.4/pluginterfaces/vst2.x/aeffectx.h"
#pragma warning(pop)

#include <balor/system/Module.hpp>
#include <balor/String.hpp>
#include <balor/locale/Charset.hpp>
#include <balor/gui/Control.hpp>
#include <balor/io/File.hpp>

#include "./HostApplication.hpp"


namespace hwm {

//! VSTプラグインを表すクラス
struct VstPlugin
{
    //! VSTプラグインのエントリポイントを表す関数の型
    //! audioMasterCallbackというホスト側のコールバック関数を
    //! 渡してこの関数を呼び出すとAEffect *というVSTプラグインの
    //! Cインターフェースのオブジェクトが返る。
	typedef AEffect * (VstPluginEntryProc)(audioMasterCallback callback);

	VstPlugin(
		balor::String module_path,
		size_t sampling_rate,
		size_t block_size,
		HostApplication *hostapp )
		:	module_(module_path.c_str())
		,	hostapp_(hostapp)
		,	is_editor_opened_(false)
		,	events_(0)
	{
		if(!module_) { throw std::runtime_error("module not found"); }
		initialize(sampling_rate, block_size);
		directory_ = balor::locale::Charset(932, true).encode(module_.directory());
	}

	~VstPlugin()
	{
		terminate();
	}

public:
	AEffect *GetEffect() { return effect_; }
	AEffect *GetEffect() const { return effect_; }

	bool	IsSynth() const { return (effect_->flags & effFlagsIsSynth) != 0; }
	bool	HasEditor() const { return (effect_->flags & effFlagsHasEditor) != 0; }

	void	OpenEditor(balor::gui::Control &parent)
	{
		parent_ = &parent;
		dispatcher(effEditOpen, 0, 0, parent_->handle(), 0);

        //! プラグインのエディターウィンドウを開く際に指定した親ウィンドウのサイズを
        //! エディターウィンドウに合わせるために、エディターウィンドウのサイズを取得する。
		ERect *rect;
		dispatcher(effEditGetRect, 0, 0, &rect, 0);

		SetWindowSize(rect->right - rect->left, rect->bottom - rect->top);

		parent.visible(true);
		is_editor_opened_ = true;
	}

	void	CloseEditor()
	{
		dispatcher(effEditClose, 0, 0, 0, 0);
		is_editor_opened_ = false;
		parent_ = nullptr;
	}

	bool	IsEditorOpened() const { return is_editor_opened_; }

    //! ホストのコールバック関数にリサイズ要求が来た際は
    //! HostApplicationクラスのハンドラによってこの関数が呼ばれる
	void	SetWindowSize(size_t width, size_t height)
	{
		parent_->size(
			parent_->sizeFromClientSize(balor::Size(width, height))
			);
	}

    //! AEffect *というプラグインのCインターフェースオブジェクトを経由して、
    //! 実際のVSTプラグイン本体に命令を投げるには、
    //! opcodeとそれに付随するパラメータを渡して、
    //! dispatcherという関数を呼び出す。
	VstIntPtr dispatcher(VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt) {
		return effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
	}

	VstIntPtr dispatcher(VstInt32 opcode, VstInt32 index, VstIntPtr value, void *ptr, float opt) const {
		return effect_->dispatcher(effect_, opcode, index, value, ptr, opt);
	}

	HostApplication & GetHost() { return *hostapp_; }
	HostApplication const & GetHost() const { return *hostapp_; }
	std::string	GetEffectName() const { return effect_name_; }
	char const * GetDirectory() const { return directory_.c_str(); }

	size_t GetProgram() const { dispatcher(effGetProgram, 0, 0, 0, 0); }
	void SetProgram(size_t index) { dispatcher(effSetProgram, 0, index, 0, 0); }
	size_t GetNumPrograms() const { return effect_->numPrograms; }
	std::string GetProgramName(size_t index) { return program_names_[index]; }

    //! ノートオンを受け取る
    //! 実際のリアルタイム音楽アプリケーションでは、
    //! ここでノート情報だけはなくさまざまなMIDI情報を
    //! 正確なタイミングで記録するようにする。
    //! 簡単のため、このアプリケーションでは、
    //! ノート情報を随時コンテナに追加し、
    //! 次の合成処理のタイミングで内部VSTプラグイン
    //! にデータが送られることを期待する実装になっている。
	void AddNoteOn(size_t note_number)
	{
		VstMidiEvent event;
		event.type = kVstMidiType;
		event.byteSize = sizeof(VstMidiEvent);
		event.flags = kVstMidiEventIsRealtime;
		event.midiData[0] = static_cast<char>(0x90u);		// note on for 1st channel
		event.midiData[1] = static_cast<char>(note_number);	// note number
		event.midiData[2] = static_cast<char>(0x64u);		// velocity
		event.midiData[3] = 0;				// unused
		event.noteLength = 0;
		event.noteOffset = 0;
		event.detune = 0;
		event.deltaFrames = 0;
		event.noteOffVelocity = 100;
		event.reserved1 = 0;
		event.reserved2 = 0;

		auto lock = get_event_buffer_lock();
		midi_events_.push_back(event);
	}

    //! ノートオンと同じ。
	void AddNoteOff(size_t note_number)
	{
		VstMidiEvent event;
		event.type = kVstMidiType;
		event.byteSize = sizeof(VstMidiEvent);
		event.flags = kVstMidiEventIsRealtime;
		event.midiData[0] = static_cast<char>(0x80u);		// note on for 1st channel
		event.midiData[1] = static_cast<char>(note_number);	// note number
		event.midiData[2] = static_cast<char>(0x64u);		// velocity
		event.midiData[3] = 0;				// unused
		event.noteLength = 0;
		event.noteOffset = 0;
		event.detune = 0;
		event.deltaFrames = 0;
		event.noteOffVelocity = 100;
		event.reserved1 = 0;
		event.reserved2 = 0;

		auto lock = get_event_buffer_lock();
		midi_events_.push_back(event);
	}

    //! オーディオの合成処理に先立ち、
    //! MIDI情報をVSTプラグイン本体に送る。
    //! この処理は下のProcesAudioと同期的に行われるべき。
    //! つまり、送信するべきイベントがある場合は、
    //! ProcessAudioの直前に一度だけこの関数が呼ばれるようにする。
	void ProcessEvents()
	{
		{
			auto lock = get_event_buffer_lock();
            //! 送信用データをVstPlugin内部のバッファに移し替え。
			std::swap(tmp_, midi_events_);
		}

        //! 送信データがなにも無ければ返る。
		if(tmp_.empty()) { return; }

        //! VstEvents型は、内部の配列を可変長配列として扱うので、
        //! 送信したいMIDIイベントの数に合わせてメモリを確保
        //!
		//! ここで確保したメモリは
        //! processReplacingが呼び出された後で解放する。
		size_t const bytes = sizeof(VstEvents) + sizeof(VstEvent *) * std::max<size_t>(tmp_.size(), 2) - 2;
		events_ = (VstEvents *)malloc(bytes);
		for(size_t i = 0; i < tmp_.size(); ++i) {
			events_->events[i] = reinterpret_cast<VstEvent *>(&tmp_[i]);
		}
		events_->numEvents = tmp_.size();
		events_->reserved = 0;

        //! イベントを送信。
		dispatcher(effProcessEvents, 0, 0, events_, 0);
	}
	
    //! オーディオ合成処理
	float ** ProcessAudio(size_t frame)
	{
		BOOST_ASSERT(frame <= output_buffers_[0].size());

        //! 入力バッファ、出力バッファ、合成するべきサンプル時間を渡して
        //! processReplacingを呼び出す。
        //! もしプラグインがdouble精度処理に対応しているならば、
        //! 初期化の段階でeffProcessPrecisionでkVstProcessPrecision64を指定し、
        //! 扱うデータ型もfloatではなくdoubleとし、
        //! ここでprocessReplacingの代わりにprocessReplacingDoubleを使用する。
		effect_->processReplacing(effect_, input_buffer_heads_.data(), output_buffer_heads_.data(), frame);

        //! 合成終了なので
        //! effProcessEventsで送信したデータを解放する。
		tmp_.clear();
		free(events_);
		events_ = nullptr;

		return output_buffer_heads_.data();
	}

private:
    //! プラグインの初期化処理
	void initialize(size_t sampling_rate, size_t block_size)
	{
        //! エントリポイント取得
		VstPluginEntryProc * proc = module_.getFunction<VstPluginEntryProc>("VSTPluginMain");
		if(!proc) {
            //! 古いタイプのVSTプラグインでは、
            //! エントリポイント名が"main"の場合がある。
			proc = module_.getFunction<VstPluginEntryProc>("main");
			if(!proc) { throw std::runtime_error("entry point not found"); }
		}

		AEffect *test = proc(&hwm::VstHostCallback);
		if(!test || test->magic != kEffectMagic) { throw std::runtime_error("not a vst plugin"); }

		effect_ = test;
        //! このアプリケーションでAEffect *を扱いやすくするため
        //! AEffectのユーザーデータ領域にこのクラスのオブジェクトのアドレスを
        //! 格納しておく。
		effect_->user = this;

        //! プラグインオープン
		dispatcher(effOpen, 0, 0, 0, 0);
        //! 設定系
		dispatcher(effSetSampleRate, 0, 0, 0, static_cast<float>(sampling_rate));
		dispatcher(effSetBlockSize, 0, block_size, 0, 0.0);
		dispatcher(effSetProcessPrecision, 0, kVstProcessPrecision32, 0, 0);
        //! プラグインの電源オン
		dispatcher(effMainsChanged, 0, true, 0, 0);
        //! processReplacingが呼び出せる状態に
		dispatcher(effStartProcess, 0, 0, 0, 0);

        //! プラグインの入力バッファ準備
		input_buffers_.resize(effect_->numInputs);
		input_buffer_heads_.resize(effect_->numInputs);
		for(int i = 0; i < effect_->numInputs; ++i) {
			input_buffers_[i].resize(block_size);
			input_buffer_heads_[i] = input_buffers_[i].data();
		}

        //! プラグインの出力バッファ準備
		output_buffers_.resize(effect_->numOutputs);
		output_buffer_heads_.resize(effect_->numOutputs);
		for(int i = 0; i < effect_->numOutputs; ++i) {
			output_buffers_[i].resize(block_size);
			output_buffer_heads_[i] = output_buffers_[i].data();
		}

        //! プラグイン名の取得
		std::array<char, kVstMaxEffectNameLen+1> namebuf = {};
		dispatcher(effGetEffectName, 0, 0, namebuf.data(), 0);
		namebuf[namebuf.size()-1] = '\0';
		effect_name_ = namebuf.data();

        //! プログラム(プラグインのパラメータのプリセット)リスト作成
		program_names_.resize(effect_->numPrograms);
		std::array<char, kVstMaxProgNameLen+1> prognamebuf = {};
		for(int i = 0; i < effect_->numPrograms; ++i) {
			VstIntPtr result = 
				dispatcher(effGetProgramNameIndexed, i, 0, prognamebuf.data(), 0);
			if(result) {
				prognamebuf[prognamebuf.size()-1] = '\0';
				program_names_[i] = std::string(prognamebuf.data());
			} else {
				program_names_[i] = "unknown";
			}
		}
	}

    //! 終了処理
	void terminate()
	{
		if(IsEditorOpened()) {
			CloseEditor();
		}
	
		dispatcher(effStopProcess, 0, 0, 0, 0);
		dispatcher(effMainsChanged, 0, false, 0, 0);
		dispatcher(effClose, 0, 0, 0, 0);
	}

private:
	HostApplication *hostapp_;
	balor::system::Module module_;
	balor::gui::Control *parent_;
	AEffect *effect_;

	std::vector<std::vector<float>>	output_buffers_;
	std::vector<std::vector<float>> input_buffers_;
	std::vector<float *>			output_buffer_heads_;
	std::vector<float *>			input_buffer_heads_;
	boost::mutex mutable			event_buffer_mutex_;
	std::vector<VstMidiEvent>		midi_events_;
	bool							is_editor_opened_;
	std::string						effect_name_;
	std::string						directory_;
	std::vector<std::string>		program_names_;
	std::vector<VstMidiEvent> tmp_;
	VstEvents *events_;

	boost::unique_lock<boost::mutex>
			get_event_buffer_lock() const { return boost::make_unique_lock(event_buffer_mutex_); }
};

}

