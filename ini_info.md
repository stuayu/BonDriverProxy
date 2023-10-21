# BonDriverの設定ファイルについて
iniファイルはサーバ側とクライアント側で使用している項目が異なります。  
それぞれのiniファイルで必要無い項目を削除したい人は、削除しても問題ありません。  
現時点での設定項目で使用しているのはそれぞれ以下の通りです。  
また、表記の無い物は使用していない項目です。  

## クライアント側
```ini
[OPTION]
ADDRESS -> オプション、デフォルト127.0.0.1 : サーバ側のアドレス
PORT -> オプション、デフォルト1192 : サーバ側の待ち受けポート
BONDRIVER -> オプション、デフォルトBonDriver_ptmr.dll : 読み込むBonDriverのファイル名
CHANNEL_LOCK -> オプション、デフォルト0 : このクライアントからの接続でのチャンネル変更の優先度 / 0～255の数字(大きな値ほど優先で同値の場合は対等、ただし255の場合は排他ロックになる)
CONNECT_TIMEOUT -> オプション、デフォルト5 : サーバが応答しない場合の接続タイムアウト時間(秒)
USE_MAGICPACKET -> オプション、デフォルト0 : WOLパケットを投げるかどうか / 1:yes 0:no

[MAGICPACKET]
TARGET_ADDRESS -> オプション、デフォルト[OPTION]のADDRESSの値 : WOLパケットの送信先アドレス
TARGET_PORT -> オプション、デフォルト[OPTION]のPORTの値 : WOLパケットの送信先ポート
TARGET_MACADDRESS -> USE_MAGICPACKETが1の場合のみ必須 : WOL対象のMACアドレス

[SYSTEM]
PACKET_FIFO_SIZE -> オプション、デフォルト64 : コマンドパケット用のキューサイズ
TS_FIFO_SIZE -> オプション、デフォルト64 : TSバッファ用のキューサイズ
TSPACKET_BUFSIZE -> オプション、デフォルト192512 : 1つのTSバッファのサイズ(バイト)
```

## サーバ側(BonDriverProxy)
```ini
[OPTION]
ADDRESS -> オプション、デフォルト127.0.0.1 : 待ち受けに利用するアドレス(「,」区切りで8つまでなら複数指定可能、また0.0.0.0を指定すると全IPv4インタフェースで、[::]を指定すると全IPv6インタフェースで待ち受ける)
PORT -> オプション、デフォルト1192 : 待ち受けポート
SANDBOXED_RELEASE -> オプション、デフォルト0 : BonDriverに対してRelease()を試みる際に、その内部で発生したAccess Violation等を無視するかどうか / 1:yes 0:no
DISABLE_UNLOAD_BONDRIVER -> オプション、デフォルト0 : ロードしたBonDriverを、使用クライアントがいなくなった時にFreeLibrary()するのを許可しないかどうか / 1:yes 0:no

[SYSTEM]
PACKET_FIFO_SIZE -> オプション、デフォルト64 : クライアント側の物と同じ
TSPACKET_BUFSIZE -> オプション、デフォルト192512 : クライアント側の物と同じ
PROCESSPRIORITY -> オプション、デフォルトNORMAL : プロセスの実行優先度の設定値 / 有効な値は高い方から順に、REALTIME, HIGH, ABOVE_NORMAL, NORMAL, BELOW_NORMAL, IDLE
THREADPRIORITY_TSREADER -> オプション、デフォルトNORMAL : TS読み込みスレッドの実行優先度の設定値 / 有効な値は高い方から順に、CRITICAL, HIGHEST, ABOVE_NORMAL, NORMAL, BELOW_NORMAL, LOWEST, IDLE
THREADPRIORITY_SENDER -> オプション、デフォルトNORMAL : 送信スレッドの実行優先度の設定値 / 同上

```

## サーバ側(BonDriverProxyEx)
```ini
[OPTION]
ADDRESS -> オプション、デフォルト127.0.0.1 : サーバ側(BonDriverProxy)の物と同じ
PORT -> オプション、デフォルト1192 : サーバ側(BonDriverProxy)の物と同じ
OPENTUNER_RETURN_DELAY -> オプション、デフォルト0 : BonDriverに対してOpenTuner()を試みた後、以後の処理を開始するまでに待つ時間(ミリ秒)
SANDBOXED_RELEASE -> オプション、デフォルト0 : サーバ側(BonDriverProxy)の物と同じ
DISABLE_UNLOAD_BONDRIVER -> オプション、デフォルト0 : サーバ側(BonDriverProxy)の物と同じ

[BONDRIVER]
00 -> 普通に考えると必須ですが、無くても起動自体は出来てしまいます : BonDriverグループ定義
01以降 -> オプション

[SYSTEM]
PACKET_FIFO_SIZE -> オプション、デフォルト64 : クライアント側の物と同じ
TSPACKET_BUFSIZE -> オプション、デフォルト192512 : クライアント側の物と同じ
PROCESSPRIORITY -> オプション、デフォルトNORMAL : サーバ側(BonDriverProxy)の物と同じ
THREADPRIORITY_TSREADER -> オプション、デフォルトNORMAL : サーバ側(BonDriverProxy)の物と同じ
THREADPRIORITY_SENDER -> オプション、デフォルトNORMAL : サーバ側(BonDriverProxy)の物と同じ
```
