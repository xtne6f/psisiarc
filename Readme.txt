psisiarc - MPEG-TSからPSI/SI等を抽出して書庫に保存する

使用法:

psisiarc [-p pids][-n prog_num_or_index][-t stream_types][-r preset][-b maxbuf_kbytes] src dest

-p pids, default=""
  抽出するTSパケットのPIDを'/'区切りで指定。
  内容がセクション形式のストリームに限る。

-n prog_num_or_index, -256<=range<=65535, default=0
  抽出するサービスを指定。
  サービスID(1以上)かPAT(Program Association Table)上の並び順(先頭を-1として-1,-2,..)を指定する。
  このオプションを0以外にすると、PATとPMT(Program Map Table)とNIT(Network Information Table)が抽出対象に加えられる。
  このオプションが0のとき"-t"オプションは無視される。

-t stream_types, 0<=range<=255, default=""
  抽出するPMT上のストリーム形式種別(Stream type)を'/'区切りで指定。
  内容がセクション形式のストリームに限る。

-r preset
  事前定義されたオプションを追加する。今のところ以下のみ。
  - "arib-data": オプション -p 17/18/20/31/36 -n -1 -t 11/12/13 と等価。データ放送の保存用。
  - "arib-epg": オプション -p 17/18/20/31/36 -n -1 と等価。番組情報の保存用。

-b maxbuf_kbytes (kbytes), 8<=range<=1048576, default=16384
  書庫を展開するとき必要になる最大メモリ占有量の目安。
  小さくしすぎると書庫の内部で分割が発生してファイルサイズが大きくなる。

src
  入力ファイル名、または"-"で標準入力。

dest
  出力書庫名、または"-"で標準出力。

説明:

たとえば
> psisiarc -r arib-data foo.m2t foo.psc
とすると、TSファイルに含まれるデータ放送の再生に必要な情報を保存できる。

その他:

ライセンスはMITとする。
