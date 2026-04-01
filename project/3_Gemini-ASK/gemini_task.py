#!/usr/bin/env python3
# -*- coding: utf-8 -*-

import os
import sys
import json
import urllib.request
import urllib.error

# 你的 Gemini API Key
# 请出入你的Key
API_KEY = " "

# 使用的 Gemini 模型版本
MODEL = "gemini-2.5-flash"
API_URL = f"https://generativelanguage.googleapis.com/v1beta/models/{MODEL}:generateContent?key={API_KEY}"

def ask_gemini(prompt):
    """
    调用 Google Gemini API 发送 prompt 并返回结果
    """
    # 构造请求体
    data = {
        "contents": [{
            "parts": [{"text": prompt}]
        }]
    }
    
    # 将字典转换为 JSON 字符串并编码为 bytes
    json_data = json.dumps(data).encode('utf-8')
    
    # 构造 HTTP 请求
    req = urllib.request.Request(
        url=API_URL,
        data=json_data,
        headers={'Content-Type': 'application/json'},
        method='POST'
    )
    
    print("正在呼叫 Gemini，请稍候...\n")
    
    try:
        # 发送请求并获取响应
        with urllib.request.urlopen(req) as response:
            result = response.read().decode('utf-8')
            result_json = json.loads(result)
            
            # 解析并提取文本内容
            # 返回的 JSON 结构通常为: {"candidates": [{"content": {"parts": [{"text": "..."}]}}]}
            try:
                text_response = result_json['candidates'][0]['content']['parts'][0]['text']
                return text_response
            except (KeyError, IndexError) as e:
                return f"解析响应失败，原始返回数据：\n{json.dumps(result_json, indent=2, ensure_ascii=False)}"
                
    except urllib.error.HTTPError as e:
        error_msg = e.read().decode('utf-8')
        return f"HTTP 错误 {e.code}: {e.reason}\n详细信息: {error_msg}"
    except urllib.error.URLError as e:
        return f"网络连接错误: {e.reason}\n请检查你的网络连接（如果需要，请配置代理）。"
    except Exception as e:
        return f"发生未知错误: {str(e)}"

def main():
    print("=== Gemini API 简单任务脚本 ===")
    print("输入 'quit' 或 'exit' 退出程序。")
    print("-" * 30)
    
    while True:
        try:
            prompt = input("\n请输入你的任务或问题: ")
            if prompt.strip().lower() in ['quit', 'exit']:
                print("再见！")
                break
            
            if not prompt.strip():
                continue
                
            response = ask_gemini(prompt)
            print("🤖 Gemini 回答:")
            print("-" * 30)
            print(response)
            print("-" * 30)
            
        except KeyboardInterrupt:
            print("\n程序已终止。")
            break
        except EOFError:
            break

if __name__ == "__main__":
    main()
