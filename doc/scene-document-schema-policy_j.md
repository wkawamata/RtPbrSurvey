# Scene Document schemaVersion 運用方針

## 現在の契約

Scene Document の保存形式は `schemaVersion: 1` である。`SceneDocument::kSchemaVersion` と一致しない文書、未知フィールド、必須フィールドの欠落は読み込まない。

読み込みは候補文書へ行い、構文・スキーマ・参照・値のいずれかの検証に失敗した場合、編集中または実行中の Scene Document を置き換えない。保存は常に、読み込み済みの現在スキーマだけを出力する。

この方針により、将来版の文書を旧アプリで読み込んで保存し、未知の情報を失うことを防ぐ。

## schemaVersion を上げる条件

次の場合に schemaVersion を上げる。

- 既存フィールドの意味、単位、座標系、既定値を変更する。
- 必須フィールドを追加、削除、または型変更する。
- Node、Asset、Material、Camera、Environment の保存構造を変更する。
- 旧形式を推測で解釈すると再現性を損なう変更を加える。

任意フィールドを追加して旧版で安全に無視できる設計は現在採用していない。未知フィールドを拒否するため、その場合も version を上げ、新旧の受理条件を明示する。

## 将来の移行手順

1. 新しい `SceneDocument` の schemaVersion を定義する。
2. 旧 version ごとの JSON を候補データへ読み込み、純粋な変換関数で新しい候補文書へ移行する。
3. 移行後の候補に対して、現行と同じ意味検証、参照検証、SceneGraph 検証を行う。
4. 検証成功時だけ候補をアプリ状態へ反映する。
5. 次回の明示 Save でのみ現行 version の JSON を書き出す。Load 時にファイルを書き換えない。

移行関数は GPU、Renderer、ファイル I/O、UI 状態に依存させない。入力 JSON と出力 `SceneDocument` を使う単体テストで検証する。

## 互換性の扱い

| 入力 | 動作 |
| --- | --- |
| 現行 version | 検証して読み込む |
| 過去に対応を宣言した version | 移行してから検証する |
| 将来 version | 読み込みを拒否し、ファイルを変更しない |
| 不明なフィールド | 読み込みを拒否し、ファイルを変更しない |
| 移行不能な過去 version | 読み込みを拒否し、必要な移行ツールを案内する |

## テスト要件

- 各対応 version の fixture を Load し、現行 `SceneDocument` へ移行できること。
- 移行後の Save → Load で意味が保持されること。
- 未知 version、未知フィールド、不正な移行入力で出力文書が変化しないこと。
- Asset / Material / Node ID、parentId、renderPreset 相対パスが移行後も有効であること。

現在は v1 のみを受理する。`SceneDocumentTests` には未知 schema を拒否し、既存の出力文書を保持する回帰テストがある。
