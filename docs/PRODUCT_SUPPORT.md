# 产品支持范围

VCamES 2.3 的产品范围固定为 Google 与 Xiaomi/Redmi/POCO、Android 11–13（API 30–33）、`arm64-v8a`、已 Root 原厂系统或同源定制 ROM。Samsung、Android 14–15 和其他厂商不进入本阶段发布门禁。

| 能力 | 第一阶段范围 | 交付条件 |
|---|---|---|
| external camera ID | Google/Xiaomi，Tensor/Qualcomm；其他 SoC 仅候选探测 | 匹配内核模块、Camera Provider、内容测试 |
| 原 front/back ID 替换 | Pixel Qualcomm/Tensor、Xiaomi Qualcomm | 精确设备 + OTA Profile、专用 adapter、全量验收 |
| Xiaomi MediaTek | 后续批次 | 独立 buffer/gralloc/Camera HAL 适配与压力验证 |

“范围内”不等于“已验证”。仓库当前没有任何非空 `VERIFIED` Profile；只有签名目录中的精确 `compatibility_id` 命中并通过 [发布门禁](RELEASE_GATES.md)，UI 才能在未来显示产品级 VERIFIED。没有 Profile 时必须保持 `UNVERIFIED` 或 `UNSUPPORTED`，不得猜测兼容。

Pixel 4/4a/5 属于 Qualcomm 路线，Pixel 6/6 Pro 属于 Tensor 路线。具体月度 OTA 仍是独立适配对象，不能跨版本复用二进制结论。
