#include <array>
#include <memory>

#include <windows.h>
#include <tchar.h>
#include <mmsystem.h>

#include <boost/atomic.hpp>
#include <boost/range/adaptors.hpp>
#include <boost/optional.hpp>
#include <boost/thread.hpp>
#include <boost/thread/condition_variable.hpp>
#include <boost/thread/locks.hpp>

#include <balor/gui/all.hpp>
#include <balor/io/File.hpp>
#include <balor/system/all.hpp>
#include <balor/locale/all.hpp>
#include <balor/graphics/all.hpp>

#pragma warning(push)
#pragma warning(disable: 4996)
#include "./vstsdk2.4/pluginterfaces/vst2.x/aeffectx.h"
#pragma warning(pop)

#include "./HostApplication.hpp"
#include "./VstPlugin.hpp"
#include "./WaveOutProcessor.hpp"

namespace hwm {

namespace io = balor::io;
namespace gui = balor::gui;
namespace sys = balor::system;
namespace gpx = balor::graphics;

//! GUI系定数
static size_t CLIENT_WIDTH = 800;
static size_t CLIENT_HEIGHT = 200;

static size_t const KEY_HEIGHT = 50;
static size_t const KEY_WIDTH = 15;
static balor::Rectangle const KEYBOARD_RECT(0, CLIENT_HEIGHT - KEY_HEIGHT, CLIENT_WIDTH, KEY_HEIGHT);

//! オーディオ系定数
static size_t const SAMPLING_RATE = 44100;
static size_t const BLOCK_SIZE = 1024;
static size_t const BUFFER_MULTIPLICITY = 4;

int main_impl()
{
	boost::mutex process_mutex;
	auto get_process_lock = [&] () -> boost::unique_lock<boost::mutex> { return boost::make_unique_lock(process_mutex); };

	gpx::Font font(L"メイリオ", 18, gpx::Font::Style::regular, gpx::Font::Quality::antialiased);
	gpx::Font font_small(L"メイリオ", 12, gpx::Font::Style::regular, gpx::Font::Quality::antialiased);

	//! メインウィンドウ
	//! この関数の後半で、
    //! 鍵盤や、プログラム(VSTプラグインのパラメータのプリセット)リストが追加される。
	gui::Frame frame(L"VstHostDemo", CLIENT_WIDTH, CLIENT_HEIGHT, gui::Frame::Style::singleLine);
	frame.icon(gpx::Icon::windowsLogo());
	frame.maximizeButton(false);

	//! ロードするVSTi選択
	gui::OpenFileDialog  file_dialog;
	file_dialog.pathMustExist(true);
	file_dialog.filter(_T("VSTi DLL(*.dll)\n*.dll\nAll Files(*.*)\n*.*\n\n"));
	file_dialog.title(_T("Select a VSTi DLL"));
	bool selected = file_dialog.show(frame);
	if(!selected) { return 0; }

	//! VSTプラグインと、ロードしているVSTホストの間でデータをやりとりするクラス
	HostApplication		hostapp(SAMPLING_RATE, BLOCK_SIZE);

	//! VstPluginクラス
	//! VSTプラグインのCインターフェースであるAEffectを保持して、ラップしている
	VstPlugin			vsti(file_dialog.filePath(), SAMPLING_RATE, BLOCK_SIZE, &hostapp);

	if(!vsti.IsSynth()) {
		gui::MessageBox::show(
			frame.handle(),
			_T("This plugin [") + 
			io::File(file_dialog.filePath()).name() +
			_T("] is an Audio Effect. VST Instrument is expected.")
			);
		return 0;
	}

	//! Wave出力クラス
	//! WindowsのWaveオーディオデバイスをオープンして、オーディオの再生を行う。
	WaveOutProcessor	wave_out_;

	//! デバイスオープン
	bool const open_device =
		wave_out_.OpenDevice(
			SAMPLING_RATE, 
			2,	//2ch
			BLOCK_SIZE,				// バッファサイズ。再生が途切れる時はこの値を増やす。ただしレイテンシは大きくなる。
			BUFFER_MULTIPLICITY,	// バッファ多重度。再生が途切れる時はこの値を増やす。ただしレイテンシは大きくなる。

			//! デバイスバッファに空きがあるときに呼ばれるコールバック関数。
			//! このアプリケーションでは、一つのVstPluginに対して合成処理を行い、合成したオーディオデータをWaveOutProcessorの再生バッファへ書き込んでいる。
			[&] (short *data, size_t device_channel, size_t sample) {

				auto lock = get_process_lock();

				//! VstPluginに追加したノートイベントを
				//! 再生用データとして実際のプラグイン内部に渡す
				vsti.ProcessEvents();
				
				//! sample分の時間のオーディオデータ合成
				float **syntheized = vsti.ProcessAudio(sample);

				size_t const channels_to_be_played = 
					std::min<size_t>(device_channel, vsti.GetEffect()->numOutputs);

				//! 合成したデータをオーディオデバイスのチャンネル数以内のデータ領域に書き出し。
				//! デバイスのサンプルタイプを16bit整数で開いているので、
				//! VST側の-1.0 .. 1.0のオーディオデータを-32768 .. 32767に変換している。
				//! また、VST側で合成したデータはチャンネルごとに列が分かれているので、
				//! Waveformオーディオデバイスに流す前にインターリーブする。
				for(size_t ch = 0; ch < channels_to_be_played; ++ch) {
					for(size_t fr = 0; fr < sample; ++fr) {
						double const sample = syntheized[ch][fr] * 32768.0;
						data[fr * device_channel + ch] =
							static_cast<short>(
								std::max<double>(-32768.0, std::min<double>(sample, 32767.0))
								);
					}
				}
			}
		);

	if(open_device == false) {
		return -1;
	}

	//! VSTは、文字列情報などをchar *で扱うが、balorではUnicodeで扱う。
	balor::String eff_name(vsti.GetEffectName(), balor::locale::Charset(932, true));

	//! frameの描画イベントハンドラ
	//! 鍵盤を描画する。
	frame.onPaint() = [eff_name] (gui::Frame::Paint& e) {
		e.graphics().pen(gpx::Color::black());

		//! draw keyboard
		for(size_t i = 0; i < 60; ++i) {
			switch(i % 12) {
			case 1: case 3: case 6: case 8: case 10:
				e.graphics().brush(gpx::Color(5, 5, 5));
				break;
			default:
				e.graphics().brush(gpx::Color(250, 240, 230));
			}

			e.graphics().drawRectangle(i * KEY_WIDTH, CLIENT_HEIGHT - KEY_HEIGHT, KEY_WIDTH, KEY_HEIGHT);
		}
	};

	//! 以下のマウスイベント系のハンドラは、鍵盤部分をクリックした時の処理など。
	//! 
	//! MIDI規格では、"鍵盤が押された"という演奏情報を「ノートオン」
	//! "鍵盤が離された"という演奏情報を「ノートオフ」というMIDIメッセージとして定義している。
	//! これらのほか、音程を変化させる「ピッチベンド」や、楽器の設定を変え、音色を変化さたりする「コントロールチェンジ」など、
	//! さまざまなMIDIメッセージを適切なタイミングでVSTプラグインに送ることで、VSTプラグインを自由に演奏できる。
	//! 
	//! このアプリケーションでは、画面上に描画した鍵盤がクリックされた時に
	//! VstPluginにノートオンを送り、鍵盤上からクリックが離れた時に、ノートオフを送っている。
	boost::optional<size_t> sent_note;

	auto getNoteNumber = [] (balor::Point const &pt) -> boost::optional<size_t> {
		if( !KEYBOARD_RECT.contains(pt) ) {
			return boost::none;
		}

		return pt.x / 15 + 0x30; //! キーボードの左端を鍵盤のC3にして、15px毎に半音上がる。
	};

	frame.onMouseDown() = [&] (gui::Frame::MouseDown &e) {
		BOOST_ASSERT(!sent_note);

		if(!e.lButton() || e.ctrl() || e.shift()) {
			return;
		}

		auto note_number = getNoteNumber(e.position());
		if(!note_number) {
			return;
		}

		e.sender().captured(true);

        //! プラグインにノートオンを設定
		vsti.AddNoteOn(note_number.get());
		sent_note = note_number;
	};

	frame.onMouseMove() = [&] (gui::Frame::MouseEvent &e) {
		if(!sent_note) {
			return;
		}

		auto note_number = getNoteNumber(e.position());
		if(!note_number) {
			return;
		}

		if(note_number == sent_note) {
			return;
		}

		vsti.AddNoteOff(sent_note.get());
		vsti.AddNoteOn(note_number.get());
		sent_note = note_number;
	};

	frame.onMouseUp() = [&] (gui::Frame::MouseUp &e) {
		if(!sent_note) {
			return;
		}

		if(e.sender().captured()) {
			e.sender().captured(false);
		}

		vsti.AddNoteOff(sent_note.get());
		sent_note = boost::none;
	};

	frame.onDeactivate() = [&] (gui::Frame::Deactivate &/*e*/) {
		if(sent_note) {
			vsti.AddNoteOff(sent_note.get());
			sent_note = boost::none;
		}
	};

	//! プラグイン名の描画
	gui::Panel plugin_name(frame, 10, 10, 125, 27);
	plugin_name.onPaint() = [&font, eff_name] (gui::Panel::Paint &e) {
		e.graphics().font(font);
		e.graphics().backTransparent(true);
		e.graphics().drawText(eff_name, e.sender().clientRectangle());
	};

	//! プログラムリストの設置
	gui::Panel program_list_label(frame, 10, 80, 75, 18);
	program_list_label.onPaint() = [&font_small] (gui::Panel::Paint &e) {
		e.graphics().font(font_small);
		e.graphics().backTransparent(true);
		e.graphics().drawText(_T("Program List"), e.sender().clientRectangle());
	};

	std::vector<std::wstring> program_names(vsti.GetNumPrograms());
	for(size_t i = 0; i < vsti.GetNumPrograms(); ++i) {
		program_names[i] = balor::locale::Charset(932, true).decode(vsti.GetProgramName(i));
	}

	gui::ComboBox program_list(frame, 10, 100, 200, 20, program_names, gui::ComboBox::Style::dropDownList);
	program_list.list().font(font_small);
	program_list.onSelect() = [&] (gui::ComboBox::Select &e) {
		int const selected = e.sender().selectedIndex();
		if(selected != -1) {
			auto lock = get_process_lock();
			vsti.SetProgram(selected);
		}
	};

	//! エディタウィンドウ
	gui::Frame editor;
	{
		//! ロードしているVSTプラグイン自身がエディタウィンドウを持っている場合のみ。
		if(vsti.HasEditor()) {
			editor = gui::Frame(eff_name, 400, 300, gui::Frame::Style::singleLine);
			editor.icon(gpx::Icon::windowsLogo());

			//メインウィンドウの下に表示
			editor.position(frame.position() + balor::Point(0, frame.size().height));
			editor.owner(&frame);
			editor.maximizeButton(false);	//! エディタウィンドウのサイズ変更不可
			//! エディタウィンドウは消さないで最小化するのみ
			editor.onClosing() = [] (gui::Frame::Closing &e) {
				e.cancel(true);
				e.sender().minimized(true);
			};
			vsti.OpenEditor(editor);
		}
	}

	//! メッセージループ
	//! frameを閉じると抜ける
	frame.runMessageLoop();

	//! 終了処理
	vsti.CloseEditor();
	wave_out_.CloseDevice();

	return 0;
}

}	//::hwm

int APIENTRY WinMain(HINSTANCE , HINSTANCE , LPSTR , int ) {

	try {
		hwm::main_impl();
	} catch(std::exception &e) {
		balor::gui::MessageBox::show(
			balor::String(
				_T("error : ")) + 
				balor::locale::Charset(932, true).decode(e.what())
				);
	}
}
