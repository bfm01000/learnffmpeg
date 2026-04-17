#!/usr/bin/env python3
import json
import os
import sys
import urllib.error
import urllib.request


API_URL = "https://open.bigmodel.cn/api/paas/v4/chat/completions"


def mask_key(key: str) -> str:
    if len(key) <= 10:
        return "*" * len(key)
    return f"{key[:6]}...{key[-4:]}"


def main() -> int:
    api_key = os.environ.get("GLM_API_KEY")
    if not api_key:
        print("未检测到环境变量 GLM_API_KEY")
        print("用法: GLM_API_KEY='你的key' python3 learnTarget/scripts/check_glm_key.py")
        return 2

    payload = {
        "model": "glm-5.1",
        "messages": [
            {
                "role": "user",
                "content": "你好，请只回复：pong"
            }
        ],
        "max_tokens": 16,
        "temperature": 0.1,
    }

    req = urllib.request.Request(
        API_URL,
        data=json.dumps(payload).encode("utf-8"),
        headers={
            "Authorization": f"Bearer {api_key}",
            "Content-Type": "application/json",
        },
        method="POST",
    )

    try:
        with urllib.request.urlopen(req, timeout=20) as resp:
            body = resp.read().decode("utf-8", errors="replace")
            data = json.loads(body)
            content = ""
            try:
                content = data["choices"][0]["message"]["content"]
            except Exception:
                content = "<未解析到回复内容>"

            print("GLM Key 可用")
            print(f"Key: {mask_key(api_key)}")
            print(f"HTTP: {resp.status}")
            print(f"Model: {data.get('model', 'unknown')}")
            print(f"Reply: {content}")
            return 0
    except urllib.error.HTTPError as e:
        body = e.read().decode("utf-8", errors="replace")
        print("GLM Key 不可用或请求失败")
        print(f"Key: {mask_key(api_key)}")
        print(f"HTTP: {e.code}")
        print(body)
        return 1
    except Exception as e:
        print("请求异常")
        print(f"Key: {mask_key(api_key)}")
        print(str(e))
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
