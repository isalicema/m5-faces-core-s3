# M5 Faces CoreS3 — 公开维护日志

> 公开仓项目日志。首次建档：2026-09-18，Asia/Shanghai（UTC+08:00）。
> 仅记录公开仓内实际发生的工作；私有研发台账、设备标识、网络配置、凭据、实机证据与恢复文件不在此处复制。

## 当前状态

- **目标：** 维护可公开构建的 M5 Faces/CoreS3 固件与可选 Mac 音乐 Bridge，提供 Music Remote、Internet Radio、Companion 和 Connection Center。
- **有效公开基线：** `982a6ea`（Connection Center 已推送至 `main`）。
- **当前工作：** 选择性移植私有研发区 2026-09-18 的 Home / Music / Radio Machiwhale UI 与稳定性修复，同时保留公开版动态 OTA 设备绑定、凭据隔离和浅色 Radio 的后续替换设计。
- **公开边界：** 不纳入 `.local/`、`.pio/`、`backups/`、`evidence/`、`build/`、设备或网络标识、令牌、私有路径、日志、私有固件或恢复包。任何公开固件都必须由本树重新构建。
- **验证状态：** 本轮尚未完成；构建、测试、提交、推送、烧录和实机观察分别记录，互不替代。

## 记录

### PUB-FACES-001 — 2026-09-18：公开同步启动

Alice 明确授权将私有研发副本中本轮 Home / Music / Radio UI 重构和相关稳定性修复，选择性、脱敏地同步到本公开仓并完成测试、commit 与 push。星子先读取该副本的项目台账和三份 UI 迁移说明，确认公开基线为 `982a6ea`，再建立本日志。

同步原则：移植运行所需源码、公开素材、生成脚本、测试和使用文档；不复制私有证据、已安装固件、具体设备 MAC、OTA 固定设备值、网络/配对信息、备份或私人日志。私有实机验收仅作为设计选择依据，不当作公开树的构建或实机验证。后续记录将追加实际移植文件、测试、提交与远端 SHA。

### PUB-FACES-002 — 2026-09-18：Paper UI 候选完成，待提交

Alice 授权的范围落实为 Home、Music Remote、Internet Radio 的纸面 UI 与连接/输入稳定性同步。星子新增公开的 `InputEventQueue`、输入诊断、面板覆盖缓存、Music paper 图标/状态模型、Radio 布局和电量遥测；更新 Bridge 预览与服务端，使 Home、Music、Radio 都能显示由设备上报的电量状态。Radio 的网页交互仅为本地模拟，不会发起 Mac 播放；公开 OTA 仍沿用运行时设备绑定，未带入固定设备值。

为避免发布 macOS 系统字体的派生字形，Radio 动态文字改用仓库内 OFL Noto 字体；四倍与原生尺寸 UI 素材从自包含的公开本地预览生成，字体资产也从仓库内的 Noto 源重新生成。README 更新为中英文、核心 UI 预览、功能/交互说明、MIT 与署名；新增 `CHANGELOG.md`。

验证（均为公开树本地结果）：19 个 C++17 Address/Undefined sanitizer 模型与交互测试通过；Python Bridge 测试 63 项通过；`node tests/music_client_test.cjs` 通过；`pio run -e suite` 通过，生成候选固件，Flash 91.0%、RAM 27.8%。使用本地 Bridge 页面实际检查 Home、Music、Radio 的预览与 Radio 的非播放承诺。未执行烧录、OTA、发布或新的实机观察；预览不替代设备验收。提交和远端推送仍待下一条记录确认。
