## 环境需求

- GNU Make,gcc
- OpenSSL
- 标准C库

## 编译和使用方法

编译与安装：
```
make
sudo make install
```

- 成功后会在 /yunba-c-sdk-master/build/output/sample/ 下生成 stdouta_demo 和 stdinpub_present 两个可执行文件。*注意在使用 make 的时候如果你的电脑有多于一个的 C 标准库，就需要在 make 的时候加上`-stdlib=libstdc++`来选择标准库，否则会产生错误。*

- 使用 bash 或其它命令行工具进入可执行文件的路径，然后执行该程序。

- stdinpub_present 的使用方法是 `stdinpub_present <topic name> --appkey <appkey> --deviceid <deviceid> --retained --qos <qos> --delimiter <delimiter>`。`<topic name>`和`<appkey>`是必须的，其余为可选项，不填的话使用默认值，其中`<deviceid>`可以使用已有的，没有的话系统会自动给您分配一个，用以在后台区分用户；`retained`默认关闭，打开后可以收到自己发送的消息；`<delimiter>`为分隔符，打出该字符后会发送该字符前的字符，默认为`\n`。
	- 示例：`./stdinpub_present test --appkey XXXXXXXXXXXXXXXXXXXXXXXX --retained`

- 运行成功后会订阅该频道，并向该频道发送一个消息，您可以在 Portal 中看到。还会向服务器询问该 topic 的 aliaslist、topic 和 status 的信息，获取完以后当您按回车之后会发送在分割符`<delimiter>`之前的字符。

- stdouta_demo 的使用方法与 stdinpub_present 类似，只是没有了向服务器查询的过程。
	- 示例：`./stdouta_demo tttest --appkey XXXXXXXXXXXXXXXXXXXXXXXX`

卸载及清理：
```
sudo make uninstall
make clean
```

## 第三方库

该sdk中使用第三方的cJSON。src/cJSON.c src/cJSON.h。

使用时请留意。

STDOUT_LOG:

```
export MQTT_C_CLIENT_TRACE=stdout

export MQTT_C_CLIENT_TRACE_LEVEL=TRACE_PROTOCOL
```

## 编译环境

在以下环境编译测试通过。

ubuntu 12.04, gcc version 4.6.4.

mac, Apple LLVM version 6.1.0／OS X 10.11.6

## 注意

该sdk 不支持标准的mqtt, mqtt v3.1.1

本例子基于 Eclipse paho
