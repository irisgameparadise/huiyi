# 參與會意注音開發 · Contributing

謝謝你願意幫忙！Thanks for helping out. Issues and pull requests in Chinese or English are both welcome.

## 開始之前

- 小修正（錯字、明顯的 bug）可以直接開 PR。
- 較大的改動（新功能、改變按鍵行為、改架構）請先開 issue 討論，避免做了白工。
- 一個 PR 只做一件事。

## 建置與測試

步驟見 [README](README.md#從原始碼建置)。開 PR 前請至少確認：

1. 64 位元與 32 位元（`-DAQ_TIP_ONLY=ON`）都建置成功、沒有新的警告。
2. `composer_test` 全部通過：
   ```powershell
   windows\build\composer_test.exe windows\build\HuiyiEngine.exe packaging\payload data\char_to_keys.json
   ```
3. 改到按鍵或組字行為時，在 `windows/tip/composer_test.cpp` 補上對應的測試案例。
4. 改到輸入法本體（`windows/tip/tsf`）時，實際安裝試打一次，並說明你在哪些程式測過（記事本、瀏覽器、Office…）。

## 程式碼位置

| 目錄 | 內容 | 授權 |
|---|---|---|
| `windows/tip` | 組字狀態機（`composer.*`）、與引擎的連線、設定 | GPL-3.0 |
| `windows/tip/tsf` | TSF text service 本體、候選窗、工作列圖示 | GPL-3.0 |
| `windows/settings` | 設定視窗 | GPL-3.0 |
| `windows/engine` | 解碼引擎（named pipe 伺服器、字典、個人記憶） | Apache-2.0 |
| `decoder/native` | AquaType 解碼器；`cpu/` 是 Windows 的 CPU 後端 | Apache-2.0 |

你送出的程式碼會依所在目錄的授權釋出。請不要貼上來源不明、或授權不相容的程式碼。

## 風格

- 跟著周圍程式碼的寫法：命名、註解密度、縮排。註解寫「為什麼」，不寫「做了什麼」。
- 輸入法跑在每個程式裡面：不要在按鍵路徑上做會卡住的事（檔案 I/O、等待引擎），不要開任何視窗或主控台。
- 不記錄使用者打的字：記錄檔、除錯輸出都一樣。

## 隱私與安全

輸入法看得到使用者打的每一個字。任何會把資料送出電腦、寫進檔案或記錄的改動，都要在 PR 裡寫清楚理由。安全性問題請不要公開開 issue，改用 GitHub 的 Security → Report a vulnerability 私下回報。
