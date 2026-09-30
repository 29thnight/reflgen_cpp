# reflgen 아이콘을 그린다 — [[◆]]: 속성 괄호가 필드(마름모)를 품은 모양. 도형만으로 그린다(폰트·외부 이미지 없음).
# 1024 로 그려 줄여서 가장자리를 부드럽게 한다. NuGet 패키지(msbuild/nuget/reflgen.nuspec)와 VS 확장
# (vs/src/Reflgen.VisualStudio)이 assets/ 의 결과를 그대로 싣는다.
#
#   python scripts/make-icon.py    # assets/icon.png(128×128, nuget.org 권장), assets/preview.png(200×200, VSIX 상세 화면)
#
# Pillow 가 필요하다(pip install pillow). 결과는 커밋한다 — 빌드가 이 스크립트를 부르지 않는다.
import pathlib

from PIL import Image, ImageDraw

ROOT = pathlib.Path(__file__).resolve().parents[1]
CANVAS = 1024
NAVY = (30, 41, 72, 255)
WHITE = (255, 255, 255, 255)
CYAN = (94, 234, 212, 255)


def icon():
    image = Image.new("RGBA", (CANVAS, CANVAS), (0, 0, 0, 0))
    draw = ImageDraw.Draw(image)
    draw.rounded_rectangle([40, 40, CANVAS - 40, CANVAS - 40], radius=210, fill=NAVY)

    # [[ 와 ]] — 두 괄호를 팔 길이보다 넓게 띄워 32px 에서도 둘로 보이게 한다.
    top, bottom, stroke, arm, spacing, left = 236, 788, 58, 92, 146, 176
    right = CANVAS - left
    for offset in (0, spacing):
        x = left + offset
        draw.rectangle([x, top, x + stroke, bottom], fill=WHITE)
        draw.rectangle([x, top, x + arm, top + stroke], fill=WHITE)
        draw.rectangle([x, bottom - stroke, x + arm, bottom], fill=WHITE)
        x = right - offset
        draw.rectangle([x - stroke, top, x, bottom], fill=WHITE)
        draw.rectangle([x - arm, top, x, top + stroke], fill=WHITE)
        draw.rectangle([x - arm, bottom - stroke, x, bottom], fill=WHITE)

    # 괄호가 품은 필드.
    center, radius = CANVAS // 2, 92
    draw.polygon([(center, center - radius), (center + radius, center), (center, center + radius),
                  (center - radius, center)], fill=CYAN)
    return image


def main():
    assets = ROOT / "assets"
    assets.mkdir(exist_ok=True)
    image = icon()
    for name, size in (("icon.png", 128), ("preview.png", 200)):
        image.resize((size, size), Image.LANCZOS).save(assets / name, optimize=True)
        print(f"assets/{name} ({size}x{size})")


if __name__ == "__main__":
    main()
