MPP的源码地址：
https://github.com/rockchip-linux/mpp

下载命令：
git clone -b release https://github.com/rockchip-linux/mpp.git

MPP_Encode.cpp实现的功能：
实时编码

MPP_Decode.cpp实现的功能：
实时解码

MPP_Enode.cpp编译命令：
g++ MPP_Encode.cpp -o encode `pkg-config opencv --cflags --libs` 
-I/home/linaro/rockchip-linux/mpp 
-I/usr/local/opencv-3.3.0/include
-L/home/linaro/rockchip-linux/mpp 
-L/usr/local/opencv-3.3.0/lib
-lrockchip_mpp -lmpp_base -lutils -fPIC -fpermissive

MPP_Decode.cpp编译命令：
g++ MPP_Decode.cpp -o decode `pkg-config opencv --cflags --libs` 
-I/home/linaro/rockchip-linux/mpp 
-I/usr/local/ffmpeg/include 
-I/usr/local/opencv-3.3.0/include 
-L/home/linaro/rockchip-linux/mpp 
-L/usr/local/ffmpeg/lib
-L/usr/local/opencv-3.3.0/lib 
-lavcodec -lavformat -lavutil -lswscale -lrockchip_mpp -lmpp_base -lutils -fPIC -fpermissive
