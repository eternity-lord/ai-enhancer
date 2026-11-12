# ساخت و اجرا روی ویندوز

> **خطای `'cmake' is not recognized`** یعنی CMake نصب نیست یا در `PATH` نیست.
> دو راه سریع، بعد ساخت. همهٔ دستورها را در **PowerShell** یا **cmd** تازه اجرا کنید.

---

## ۱) نصب پیش‌نیازها (یک‌بار)

### الف) CMake

```powershell
winget install --id Kitware.CMake -e
```
یا بدون winget:

```powershell
py -m pip install --upgrade cmake
```

اگر نصب‌کنندهٔ رسمی را از [cmake.org/download](https://cmake.org/download/) گرفتید،
در مرحلهٔ نصب تیک **Add CMake to the system PATH** را بزنید.

> بعد از نصب، پنجرهٔ ترمینال را **ببندید و پنجرهٔ تازه باز کنید**؛ وگرنه `PATH`
> قدیمی می‌ماند و همان خطا را دوباره می‌بینید.

### ب) کامپایلر C++ (Microsoft Visual C++)

CMake به‌تنهایی کامپایلر نیست. اگر Visual Studio ندارید، «Build Tools» کافی است:

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools"
```

این بسته حدود ۲ تا ۳ گیگابایت است و باید صبر کنید تا تمام شود.

---

## ۲) ساخت

```bat
tools\windows\build.bat
```

اسکریپت این کارها را می‌کند:

1. CMake را در `PATH` و مسیرهای متعارف پیدا می‌کند (و اگر پیدا نکرد، می‌گوید چه کنید).
2. `core` را با `Release` پیکربندی و می‌سازد (ژنراتور Visual Studio را خودش انتخاب می‌کند).
3. `atlas-cli.exe` و `atlas-tests.exe` را پیدا می‌کند — حتی وقتی خروجی داخل
   `build\Release\` افتاده باشد.
4. تست‌های هسته را اجرا می‌کند و خلاصه می‌دهد.

گزینه‌ها:

```bat
tools\windows\build.bat --install     :: نصب CMake با winget، بعد ساخت
tools\windows\build.bat --run         :: ساخت، بعد باز کردن پوستهٔ دسکتاپ
```

خروجی موفق:

```
done.  atlas-cli = build\Release\atlas-cli.exe
```

---

## ۳) اجرا

```bat
tools\windows\run-desktop.bat
```

یک پنجرهٔ «Atlas server» باز می‌شود و مرورگر روی `http://localhost:8000` بالا
می‌آید. برای خاموش‌کردن، همان پنجره را ببندید.

خط فرمان هم کار می‌کند:

```bat
build\Release\atlas-cli.exe version
build\Release\atlas-cli.exe devices
build\Release\atlas-cli.exe restore photo.jpg clean.png
build\Release\atlas-cli.exe enhance photo.jpg out4x.png --scale 4
```

---

## ۴) اگر خطا گرفتید

| خطا | معنی | کار |
|---|---|---|
| `'cmake' is not recognized` | CMake نصب/PATH نیست | بخش ۱-الف، بعد ترمینال تازه |
| `No CMAKE_CXX_COMPILER could be found` | کامپایلر نیست | بخش ۱-ب (Build Tools + workload VCTools) |
| `error C2039 ... C++20` | MSVC قدیمی | Visual Studio 2022 (۱۷٫۶ یا بالاتر) |
| ساخت است ولی `atlas-cli.exe` پیدا نمی‌شود | خروجی جای دیگری است | `dir /s build\atlas-cli.exe` و با `--run` تست کنید |
| مرورگر باز می‌شود ولی «atlas-cli پیدا نشد» | اجرای سرور از پوشهٔ دیگر | `run-desktop.bat` را از ریشهٔ ریپو اجرا کنید یا `ATLAS_CLI` را ست کنید |
| متن فارسی در cmd به‌هم‌ریخته | کد‌پیج کنسول | با `chcp 65001` یا در Windows Terminal |

---

## ۵) چرا اسکریپت‌ها انگلیسی‌اند؟

فایل‌های `.bat` با UTF-8 در `cmd` همیشه قابل‌اعتماد نیستند (بسته به کد‌پیج و BOM
ممکن است دستورها را خراب کنند). به‌همین‌دلیل متن داخل اسکریپت‌ها انگلیسی و
کوتاه است، و توضیح فارسی همین‌جاست.

---

## ۶) ساخت دستی (اگر اسکریپت را نمی‌خواهید)

```powershell
cmake -S core -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release -j
.\build\Release\atlas-tests.exe
.\build\Release\atlas-cli.exe version
python desktop\server.py --port 8000
```
