# ESP32S3_ECM_V1 GitHub 上传规范

## 提交前检查

1. 确认 ECM 相关改动未混入无关实验
2. 更新 版本记录.md
3. 更新 记忆文档.md（如有新的硬件/软件经验）
4. 确保 build/、managed_components/、sdkconfig 不会误上传
5. 至少运行一次 idf.py build

## 提交习惯

- Commit message 简短具体
- 推荐格式：
  - feat: wire cellular_ecm into app startup
  - fix: update softap dns after ecm got ip
  - docs: record ec200a ecm interface notes

## 推送习惯

1. git status
2. git add ...
3. git commit -m "type: summary"
4. git push origin main

## 必须保持更新的文档

- README.md
- 记忆文档.md
- 版本记录.md

## 严格执行

- 不上传临时测试垃圾
- 不在一个 commit 中混合无关改动
- 硬件行为有变化必须先文档化