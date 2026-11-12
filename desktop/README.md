# Atlas Desktop — پوستهٔ دسکتاپ با زبان بصری Liquid Glass

پوستهٔ گرافیکی پروژه: یک پنجرهٔ شیشه‌ای (پنل‌های نیمه‌شفاف، بلور پس‌زمینه، لبهٔ نورانی)
که همهٔ پردازش را **روی همین دستگاه** انجام می‌دهد. هیچ CDN، فونت آنلاین یا آپلود
بیرونی در کار نیست.

```
desktop/
├── server.py        سرور محلی (فقط stdlib) + پل به atlas-cli
├── test_server.py   ۷ تست دودی برای همین سرور
└── web/
    ├── index.html   ساختار سه‌پنلی: کتابخانه / صحنه / بازرس
    ├── styles.css   متریال شیشهٔ مایع + چیدمان واکنش‌گرا
    ├── app.js       آپلود، مقایسه، پردازش واقعی، نمایش اعداد سنجش
    └── test_dom.mjs ۱۸ بررسی DOM (اختیاری، jsdom)
```

## اجرا

```bash
# ۱) هسته را بسازید (اگر نکرده‌اید)
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# ۲) سرور محلی را اجرا کنید
python3 desktop/server.py --port 8000
# → http://localhost:8000
```

مسیر `atlas-cli` به‌ترتیب از `ATLAS_CLI`، `build/`، `build-ncnn/`، `build-nowebp/`
پیدا می‌شود. داده‌های کار در `desktop/.data/` می‌مانند (در گیت نیست) و با
`ATLAS_UI_DATA=/path` قابل جابه‌جایی است.

## تست

```bash
python3 desktop/test_server.py     # ۷ تست: health، آپلود، پردازش ۱×/۲×، خطاها

# اختیاری: ۱۸ بررسی DOM بدون مرورگر (نیاز به یک‌بار npm install)
cd desktop && npm install && npm run test:dom
```

## API محلی

| متد | مسیر | کار |
|---|---|---|
| GET | `/api/health` | وضعیت + مسیر atlas-cli + فهرست تصاویر |
| GET | `/api/devices` | خروجی `atlas-cli devices` |
| POST | `/api/upload?name=…` | بدنهٔ خام تصویر → آیتم جدید + پیش‌نمای PNG |
| POST | `/api/process` | `{id,denoise,deblock,scale,quality}` → نتیجه + متریک‌ها |
| GET | `/img/<id>/…?v=preview\|result` | تصویر پیش/پس |

همهٔ اعداد σ، blockiness و زمان از خروجی JSON خود `atlas-cli` می‌آیند؛ UI هیچ عددی
را حدس نمی‌زند یا هاردکد نمی‌کند.

## چرا وب و نه Tauri (فعلاً)

Tauri به Rust + WebView سیستمی نیاز دارد و در محیط ساخت فعلی موجود نیست. اما
معماری عمداً طوری است که مهاجرت ارزان باشد: `web/` بدون هیچ تغییر در یک
WebView داخل Tauri اجرا می‌شود و همین `/api/*` با همان `atlas-cli` کار می‌کند.
هسته (C++) در هر دو حالت دست‌نخورده است. جزئیات و محدودیت‌ها:
[../docs/21-desktop-ui.md](../docs/21-desktop-ui.md).
