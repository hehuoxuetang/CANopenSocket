# CANopenSocket

## 背景

​	习惯在windows系统下进行开发和调试。项目使用的是CAN盒，CAN盒与PC通过LAN连接。

- 希望调试工具支持CiA 309-3 ASCII 协议，而不是只能使用CAN的原始报文； 
- 希望驱动层能支持socket，因为CAN盒与PC通过socket通讯。

## main参数

- 参数1——ip:port，作为服务器，等待客户端连接，例如0.0.0.0:6000

    作为调试工具的用户交互接口，使用TCP调试工具，如SSCOM。

- 参数2——ip:port，作为客户端，主动连接此参数配置的服务器，例如192.168.1.125:6001

    连接CAN盒的参数配置，具体根据CAN盒的要求而定。

## 功能描述

​	本程序即作为socket的服务器也作为客户端。作为服务器，采用参数1的配置，标记为server_local，等待客户端连接，只支持1个客户端。连接成功后，客户端标记为client_remote，开始等待报文。作为客户端，标记为client_local。程序启动后主动连接参数2指定的服务器，此服务器标记为server_remote。连接成功后，client_local也开始等待报文。server_local收到报文，则转发给server_remote；client_local收到报文，则转发为client_remote。

