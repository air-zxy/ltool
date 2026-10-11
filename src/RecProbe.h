#pragma once

#include <QString>

// F2 录屏之前的可行性探针（`ltool.exe --recprobe`）：三件事都必须实测，不能靠文档猜。
//  1) Media Foundation 能不能在这台机器上写出 MP4/H.264（mfh264enc.dll 在不在不算证据）；
//  2) 有没有 AAC 编码器（没音轨的录屏价值减半，有音轨又得知道容器里塞不塞得进）；
//  3) "只录某一个软件的页面"——Windows.Graphics.Capture 按窗口取帧在这台机器上能不能用、
//     帧到得多快、拷一次到 CPU 要多久（这决定能给的帧率）。
namespace RecProbe {

// 跑一遍，返回人话报告（每行一条结论）。targetSeconds：按窗口取帧观察多久。
QString run(int observeMs);

} // namespace RecProbe
