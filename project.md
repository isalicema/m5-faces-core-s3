# M5 Faces CoreS3 — 公开维护日志

> 公开仓项目日志。首次建档：2026-09-18，Asia/Shanghai（UTC+08:00）。
> 仅记录公开仓内实际发生的工作；私有研发台账、设备标识、网络配置、凭据、实机证据与恢复文件不在此处复制。

## 当前状态

- **目标：** 维护可公开构建的 M5 Faces/CoreS3 固件与可选 Mac 音乐 Bridge，提供 Music Remote、Internet Radio、Companion 和 Connection Center。
- **有效公开版本：** `2859def`（Paper UI 同步与发布记录已推送至 `main`）。
- **当前工作：** 本轮同步已完成；后续维护继续保留公开版动态 OTA 设备绑定、凭据隔离和纸面 Radio 设计。
- **公开边界：** 不纳入 `.local/`、`.pio/`、`backups/`、`evidence/`、`build/`、设备或网络标识、令牌、私有路径、日志、私有固件或恢复包。任何公开固件都必须由本树重新构建。
- **验证状态：** 本轮尚未完成；构建、测试、提交、推送、烧录和实机观察分别记录，互不替代。

## 记录

### PUB-FACES-001 — 2026-09-18：公开同步启动

Alice 明确授权将私有研发副本中本轮 Home / Music / Radio UI 重构和相关稳定性修复，选择性、脱敏地同步到本公开仓并完成测试、commit 与 push。星子先读取该副本的项目台账和三份 UI 迁移说明，确认公开基线为 `982a6ea`，再建立本日志。

同步原则：移植运行所需源码、公开素材、生成脚本、测试和使用文档；不复制私有证据、已安装固件、具体设备 MAC、OTA 固定设备值、网络/配对信息、备份或私人日志。私有实机验收仅作为设计选择依据，不当作公开树的构建或实机验证。后续记录将追加实际移植文件、测试、提交与远端 SHA。

### PUB-FACES-002 — 2026-09-18：Paper UI 候选完成

Alice 授权的范围落实为 Home、Music Remote、Internet Radio 的纸面 UI 与连接/输入稳定性同步。星子新增公开的 `InputEventQueue`、输入诊断、面板覆盖缓存、Music paper 图标/状态模型、Radio 布局和电量遥测；更新 Bridge 预览与服务端，使 Home、Music、Radio 都能显示由设备上报的电量状态。Radio 的网页交互仅为本地模拟，不会发起 Mac 播放；公开 OTA 仍沿用运行时设备绑定，未带入固定设备值。

为避免发布 macOS 系统字体的派生字形，Radio 动态文字改用仓库内 OFL Noto 字体；四倍与原生尺寸 UI 素材从自包含的公开本地预览生成，字体资产也从仓库内的 Noto 源重新生成。README 更新为中英文、核心 UI 预览、功能/交互说明、MIT 与署名；新增 `CHANGELOG.md`。

验证（均为公开树本地结果）：19 个 C++17 Address/Undefined sanitizer 模型与交互测试通过；Python Bridge 测试 63 项通过；`node tests/music_client_test.cjs` 通过；`pio run -e suite` 通过，生成候选固件，Flash 91.0%、RAM 27.8%。使用本地 Bridge 页面实际检查 Home、Music、Radio 的预览与 Radio 的非播放承诺。未执行烧录、OTA、发布或新的实机观察；预览不替代设备验收。

### PUB-FACES-003 — 2026-09-18：提交与推送

本轮公开源码、素材、测试和文档已由 Alice 既有 Git 身份提交为 `8071dfb`（`Sync paper UI and reliability updates`，含 Codex 协作署名），并推送到 `origin/main`。提交前暂存范围排除了私有目录、构建输出、固件和设备信息；静态检查通过。原生 Git 的 dry-run 探针一度遇到 TLS 传输错误，随后 GitHub 只读身份核验成功，原生 Git 推送与 `ls-remote` 均确认远端 `main` 为同一 SHA。此记录本身待提交，以保存该发布证据。

### PUB-FACES-004 — 2026-09-18：Music README 预览更正

Alice 指出 README 的 Music Remote 配图仍是旧版。星子使用本公开树的本地 Bridge 页面，以当前读取到的网易云音乐元数据、封面和歌词行截取 340×260 的核心屏幕区域，替换 `assets/preview-music.png`；不保留浏览器外围留白，也不写入任何控制操作。图片经尺寸、哈希和目视检查确认。图片更新已提交为 `6e6fc2f` 并推送至 `origin/main`，远端 SHA 已核对一致；未涉及固件构建、烧录或新的设备验收。
