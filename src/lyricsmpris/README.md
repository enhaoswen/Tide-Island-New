# lyricsmpris

无 Qt 的 C++23 静态库。歌曲状态、MPRIS 控制与行级歌词在宿主进程内处理，
不启动子进程，不创建常驻工作线程，不提供 UI 或独立运行程序。

`Client.hpp` 是模块入口，`Lyrics.hpp` 可以单独用于歌词解析。
目前已加入项目的 CMake 构建，尚未接入 `API`、`Timer` 或显示层。

## 状态与控制

```cpp
#include "lyricsmpris/Client.hpp"

auto result = lyricsmpris::Client::create();
if (!result) {
    // result.error(): 初始化失败，例如没有可用的 session bus。
    return;
}
auto music = std::move(*result);

// 必须通过宿主事件循环调用 process()，见下一节。
const auto& state = music.state();
// state.title / state.artists / state.album / state.track_id
// state.artwork_url / state.duration / state.playback / state.capabilities
auto progress = music.position();

const auto& lyrics = music.lyrics();
for (std::size_t i = 0; i < lyrics.size(); ++i) {
    auto line = lyrics.line(i);
    // line.text: UTF-8 string_view。
    // line.start_time: optional<milliseconds>，同步歌词每行都有开始时间。
}
auto current_text = music.current_lyric();

auto play = music.play();
auto pause = music.pause();
auto toggle = music.play_pause();
auto next = music.next();
auto previous = music.previous();
```

封面字段保存 MPRIS `mpris:artUrl` 的原始 URI，可能是 HTTP(S) 或 `file://`。
图片下载、解码及纹理管理交给显示层。作者使用 `xesam:artist`，保留多个歌手。
未知总时长用 `nullopt`，没有播放器用 `has_player == false`。

控制函数异步发送命令：返回成功表示已发送，并非播放器已经完成操作。
实际状态由之后的 MPRIS 信号更新；播放器拒绝命令时由 `last_error` 和
`Changes::Error` 报告。调用前检查播放器能力，不支持的操作返回 `Unsupported`。

## 宿主事件循环

每轮先调用 `process()` 处理已就绪事件，再取得 `poll_fds()` 和 `next_deadline()`。
将音乐 fd 复制到宿主的 `pollfd` 数组，和 Wayland、timerfd 一起等待。
取宿主定时任务与音乐截止时间的较早值作为等待期限，向上取整为毫秒。
收到音乐 fd 事件或音乐定时任务到期时，再调用 `process(ready)`。
仅时间到期时可以传入空 span。

```cpp
auto changed = music.process(ready_music_fds);
if (!changed) {
    // 总线不可用等模块级错误；宿主决定是否重建 Client。
} else if (lyricsmpris::contains(*changed, lyricsmpris::Changes::Line)
        || lyricsmpris::contains(*changed, lyricsmpris::Changes::Lyrics)) {
    // 更新歌词显示。
}
```

`process()` 不等待网络和 D-Bus 回复。解析和本地文件读取在换歌/收到歌词时进行。
本地读取仅接受有大小限制的普通文件，可用 `local_lyrics = false` 禁用。
歌曲进度按单调时钟和 `Rate` 推算；`Seeked`、换歌和播放状态变化用于校正。
歌词计时直接指向下一行开始时间，暂停后没有歌词计时任务。
遇到缺少正确 `Seeked` 信号的播放器，可以设置 `position_resync_interval`；默认不轮询。
连续进度条的刷新频率由 UI 决定，不会产生逐帧 D-Bus 请求。

所有函数由同一线程调用。`State` 引用、歌词文本视图和 fd span 不应跨修改模块的
调用保存；需要长期保留的文本请自行复制。`Client` 可移动、不可复制，析构取消请求
并释放资源。移动后的对象仅可销毁或重新赋值。

## 歌词与资源策略

- 优先使用播放器自带歌词、音频旁的 `.lrc`，随后按配置顺序尝试在线源。
- 支持 LRCLIB、LRCX、网易云、QQ、酷狗；Musixmatch 需要显式加入 `providers`
  并设置 `musixmatch_api_key`。在线源沿用原项目端点，可用性受远端服务影响。
- 支持多个时间标签、毫秒、小数时间、LRC offset、空歌词行、纯文本回退；
  LRCX 的简单 `<p begin="…">` TTML 会转为行级歌词。不保留逐字时间。
- 同一时间戳的行保留稳定顺序，`line_at()` 选最后一行。需要原文/翻译同时显示时，
  显示层可取相同时间戳的所有行。
- 标题、作者、时长和版本信息用于匹配；简繁与 Unicode 归一化只用于比较，
  显示文本保留原文。不会把没有 `[ti:]` 等匹配证据的 LRCX 裸文本当作可靠结果。
- 默认最多 2 个并发请求，最多 16 个在途/排队请求；每响应和本地歌词最多 1 MiB，
  每文档最多 10000 行。接收时执行大小检查，压缩 HTTP 响应按解压后的大小限制。
- 文本集中在一个缓冲区，时间戳用紧凑索引存储。重复时间标签共享文本。
  不缓存历史歌曲、封面位图或完整 JSON 响应，不启用 provider cookie 存储。
- 换歌、销毁、找到可信同步歌词时取消剩余请求；响应 ID 和播放器实例/版本检查
  阻止旧回复覆盖新状态。搜索失败最多重试 3 次，间隔默认 2.5s、7.5s。
- `retry_lyrics()` 重新读取当前歌曲的歌词，`set_lyric_offset()` 调整显示偏移。
  正偏移使歌词延后显示，范围为 ±1 小时。

## 构建与验证

依赖 `libsystemd`、`libcurl`、`json-c`、`utf8proc`。公开头文件不包含这些依赖的类型。
libcurl 必须支持异步 DNS；系统 libcurl 的 DNS 后端可能临时使用解析线程。

模块可以单独构建，不依赖项目的 Wayland/OpenGL 部分：

```sh
cmake -S src/lyricsmpris -B /tmp/lyricsmpris-build \
    -DCMAKE_BUILD_TYPE=Release -DLYRICSMPRIS_BUILD_TESTS=ON
cmake --build /tmp/lyricsmpris-build
ctest --test-dir /tmp/lyricsmpris-build --output-on-failure
```

之后接入程序时链接 `LyricsMpris::LyricsMpris` 即可。
测试包含歌词与源响应解析、本地 HTTP 的限制/取消/超时/cookie 行为，以及独立
session bus 中的模拟播放器，覆盖控制、进度、歌词晚到、重试和迟到回复。
