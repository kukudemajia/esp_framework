cd /d %~dp0

D:\esp\nanopb-0.3.9.4-windows-x86\nanopb-0.3.9.4-windows-x86\generator-bin\protoc.exe --nanopb_out=. GlobalConfig.proto
copy GlobalConfig.pb.h ..\include /y
copy GlobalConfig.pb.c ..\src /y
del GlobalConfig.pb.h
del GlobalConfig.pb.c
