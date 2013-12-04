# VstHostDemo

## What's this.

C++ Advent Calendar 2013のネタとして作成したVSTホストアプリケーションです。
VSTiをロードして、音を鳴らせます。

## プログラムのビルド方法

1. [Steinberg](http://japan.steinberg.net/)から、VST 2.4のSDKをダウンロードし、ソリューション内のvstsdk2.4に展開したファイルをコピーする。

2. [Boost](http://www.boost.org/)から、Boost.1.53.0をダウンロードしビルドする。(試してはいないが、1.54.0, 1.55.0でも問題ないだろうと思われる。)

3. [balorライブラリの公式ブログ](http://d.hatena.ne.jp/syanji/20110731/1312105612)から、balor 1.0.1をダウンロードし、ビルドする。

4. VstHostDemo.slnを開き、Boost, balorのインクルードディレクトリ、ライブラリディレクトリを設定する。

5. VstHostDemoプロジェクトをビルドする。

## ライセンス

このソースコードは、Boost Software License, Version 1.0で公開します。
