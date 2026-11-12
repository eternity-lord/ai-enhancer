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
tools\windows\build.bat            :: پیکربندی + ساخت + ۸۵ تست هسته
tools\windows\build.bat --doctor   :: فقط بگو CMake و کدام کامپایلر را می‌بیند
tools\windows\build.bat --clean    :: پاک‌کردن build\ (بعد از خطای generator)
tools\windows\build.bat --install  :: نصب CMake + Build Tools با winget، بعد ساخت
tools\windows\build.bat --run      :: ساخت، بعد باز کردن پوستهٔ دسکتاپ
```

> **چرا این اسکریپت ژنراتور را خودش انتخاب می‌کند؟** CMake روی ویندوز وقتی
> Visual Studio پیدا نکند، `NMake Makefiles` را انتخاب می‌کند و `nmake` فقط داخل
> «Developer Command Prompt» وجود دارد. نتیجه همان خطایی است که می‌بینید:
>
> ```
> -- Building for: NMake Makefiles
> 'nmake' '-?' failed with: no such file or directory
> CMake Error: CMAKE_CXX_COMPILER not set
> ```
>
> یعنی **کامپایلر C++ نصب نیست**. اسکریپت حالا با `vswhere` وجود MSVC را
> می‌فهمد و صریح `-G "Visual Studio 17 2022" -A x64` می‌دهد؛ اگر MSVC نبود و
> MinGW/LLVM دید، `Ninja` یا `MinGW Makefiles` را انتخاب می‌کند؛ و اگر هیچ‌کدام
> نبود، با پیام روشن متوقف می‌شود (به‌جای خطای گنگ).

### اگر همان خطای `NMake` را گرفتید

```powershell
winget install Microsoft.VisualStudio.2022.BuildTools --override "--quiet --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
```

بعد (لازم نیست پنجره را ببندید؛ اسکریپت VS را با `vswhere` پیدا می‌کند، ولی اگر
نصب تازه تمام شده یک پنجرهٔ تازه مطمئن‌تر است):

```bat
tools\windows\build.bat --clean
```

### اگر winget می‌گوید «نصب است» ولی CMake می‌گوید «could not find any instance of Visual Studio»

این خطا یعنی بستهٔ Build Tools در فهرست ویندوز ثبت شده، اما **workload «C++»**
نصب نشده یا نصب نیمه‌کاره مانده است. `winget install` دوباره کاری نمی‌کند
(«Found an existing package already installed») و باید با `--force` یا با
بوت‌استرپر رسمی نصب شود:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File tools\windows\install-msvc.ps1
```

این اسکریپت اول `winget --force` را امتحان می‌کند، بعد بوت‌استرپر رسمی مایکروسافت
را دانلود و با `--add Microsoft.VisualStudio.Workload.VCTools --includeRecommended`
اجرا می‌کند، و در پایان با `vswhere` بررسی می‌کند که واقعاً نصب شده باشد.

همان کار به‌صورت دستی:

```powershell
$bt = "$env:TEMP\vs_buildtools.exe"
Invoke-WebRequest -Uri https://aka.ms/vs/17/release/vs_buildtools.exe -OutFile $bt
Start-Process -Wait -FilePath $bt -ArgumentList '--quiet','--wait','--norestart','--nocache','--add','Microsoft.VisualStudio.Workload.VCTools','--includeRecommended'

# بررسی واقعی:
& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
```

اگر دستور آخر یک مسیر چاپ کرد، کار تمام است:

```bat
tools\windows\build.bat --clean
```

### راه سبک: MinGW به‌جای Visual Studio (حدود ۱۵۰ مگابایت)

اگر Visual Studio سنگین است یا نصبش مشکل دارد، GCC کافی است:

```powershell
winget install --id BrechtSanders.WinLibs.POSIX.UCRT -e
winget install --id Ninja-build.Ninja -e
```

بعد یک پنجرهٔ تازه باز کنید و:

```bat
tools\windows\build.bat --clean
```

اسکریپت `g++` و `ninja` را می‌بیند و با `-G Ninja` می‌سازد. (CMakeLists پروژه حالا
`Threads::Threads` را لینک می‌کند تا `std::thread` روی MinGW هم بدون خطای لینک
کار کند.)

### مسیر پروژه

پوشه‌ای مثل `Downloads\ai-enhancer-main (1)\...` پرانتز و فاصله دارد. CMake با
آن کنار می‌آید، ولی MinGW/NMake گاهی نه. اگر خطای عجیب دیدید، پروژه را به
`C:\atlas` منتقل کنید و دوباره `build.bat` بزنید.

## ساخت دستی (بدون اسکریپت)

```bat
tools\windows\build.bat
```

اسکریپت این کارها را می‌کند:

1. CMake را در `PATH` و مسیرهای متعارف پیدا می‌کند (و اگر پیدا نکرد، می‌گوید چه کنید).
2. کامپایلر را تشخیص می‌دهد (MSVC با `vswhere`، بعد `cl`، بعد `g++`/`clang++`) و
   ژنراتور مناسب را صریح انتخاب می‌کند.
3. اگر `build\` با ژنراتور دیگری پیکربندی شده باشد، خودش پاک می‌کند.
4. `atlas-cli.exe` و `atlas-tests.exe` را پیدا می‌کند — حتی وقتی خروجی داخل
   `build\Release\` افتاده باشد.
5. تست‌های هسته را اجرا می‌کند و خلاصه می‌دهد.

خروجی موفق:

```
done.  atlas-cli = build\Release\atlas-cli.exe
```

---

## ۳) اجرا

```bat
tools\windows\run-desktop.bat
```

پنجرهٔ نیتیو Atlas باز می‌شود (pywebview + WebView2؛ اگر نصب نباشد همان‌جا
`pip install pywebview` می‌کند). اگر پنجرهٔ بومی باز نشد، اسکریپت به‌صورت خودکار
سرور را بالا می‌آورد و مرورگر را روی `http://localhost:8000` باز می‌کند.
برای خاموش‌کردن، پنجرهٔ برنامه (یا پنجرهٔ «Atlas server») را ببندید. پس از
به‌روزرسانی، `atlas-cli.exe` تازه باید از `tools\windows\build.bat` بیاید.

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
| `-- Building for: NMake Makefiles` و بعد `'nmake' ... no such file or directory` | کامپایلر C++ نیست؛ CMake به NMake افتاده | بخش ۱-ب، بعد `build.bat --clean` |
| `No CMAKE_CXX_COMPILER could be found` | کامپایلر نیست | بخش ۱-ب (Build Tools + workload VCTools) |
| `generator ... does not match the cache` | `build\` با ژنراتور دیگری ساخته شده | `build.bat --clean` |
| `could not find any instance of Visual Studio` | بستهٔ Build Tools ثبت شده ولی workload «C++» نصب نیست | `install-msvc.ps1` (بخش بعد) یا `build.bat --install` |
| `winget ... No available upgrade found` | همان حالت بالا؛ winget چیزی برای ارتقا نمی‌بیند | `winget ... --force` یا بوت‌استرپر رسمی |
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
