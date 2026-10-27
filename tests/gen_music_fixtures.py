// tests/gen_music_fixtures.py —— 重新生成 tests/music_fixtures.h。
#
# 真实抓包样本不入库(体积与内容随接口演变),此脚本负责把
# tmp/fixtures/ 下的抓包字节转成 C 字符串字面量头文件。
# 重新抓包(在能访问酷我的网络环境执行,需代理时自行加 proxychains):
#   curl -o tmp/fixtures/search_ascii.json \
#     'http://search.kuwo.cn/r.s?all=jay&ft=music&client=kt&cluster=0&strategy=2012&rformat=json&encoding=utf8&issubtitle=1&vipver=1&mobi=1&pn=0&rn=2&show_copyright_off=1'
#   curl -o tmp/fixtures/chart.json \
#     'http://kbangserver.kuwo.cn/ksong.s?from=pc&fmt=json&ip=root&id=16&pn=0&rn=2'
#   curl -o tmp/fixtures/resolve.json \
#     'http://antiserver.kuwo.cn/anti.s?type=convert_url3&rid=MUSIC_661608480&format=mp3&response=url'
# 然后运行:python3 tests/gen_music_fixtures.py
import pathlib

FIXTURES = [
    ("FIXTURE_SEARCH", "search_ascii.json"),
    ("FIXTURE_CHART", "chart.json"),
    ("FIXTURE_RESOLVE", "resolve.json"),
]


def c_literal(data: bytes) -> str:
    out = []
    line = '"'

    def flush():
        nonlocal line
        if line != '"':
            out.append(line + '"')
            line = '"'

    for byte in data:
        ch = chr(byte)
        esc = {'"': '\\"', '\\': '\\\\', '\n': '\\n', '\r': '\\r', '\t': '\\t'}.get(ch)
        if esc:
            s = esc
        elif 32 <= byte < 127:
            s = ch
        else:
            # 每个非 ASCII 字节独立成字面量,杜绝 \xNN 与后续十六进制字符粘连
            flush()
            out.append('"\\x%02x"' % byte)
            continue
        if len(line) + len(s) > 72:
            flush()
        line += s
    flush()
    return "\n".join(out)


def main() -> None:
    fixtures_dir = pathlib.Path("tmp/fixtures")
    header = pathlib.Path("tests/music_fixtures.h")
    parts = [
        "// tests/music_fixtures.h —— 酷我接口真实响应字节(测试回放用)。\n",
        f"// 由 tests/gen_music_fixtures.py 生成,勿手改。\n",
        "#pragma once\n\n",
    ]
    for name, fname in FIXTURES:
        data = (fixtures_dir / fname).read_bytes()
        parts.append(f"// 来源:{fname}(抓包原样字节,{len(data)} 字节)\n")
        parts.append(f"static const char {name}[] =\n{c_literal(data)};\n\n")
    header.write_text("".join(parts), encoding="ascii")
    print(f"generated {header} ({header.stat().st_size} bytes)")


if __name__ == "__main__":
    main()
