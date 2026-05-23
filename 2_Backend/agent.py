from openai import OpenAI

# 智谱 AI 的配置
client = OpenAI(
    api_key="<你的api_key>", 
    base_url="https://open.bigmodel.cn/api/paas/v4/" # 智谱的官方 V4 接口地址
)

def ask_agent(prompt, temp, status):
    try:
        print(f">>> 正在请求智谱 AI 诊断 (温度: {temp})...")
        response = client.chat.completions.create(
            model="glm-4",  # 使用 glm-4 模型，性能非常强
            messages=[
                {"role": "system", "content": "你是一个专业的工业物联网专家。"},
                {"role": "user", "content": f"{prompt}\n当前数据：温度{temp}℃，设备状态{status}"}
            ],
            top_p=0.7,
            temperature=0.9
        )
        return response.choices[0].message.content
    except Exception as e:
        print(f"智谱 AI 调用失败: {e}")
        return f"诊断服务暂时不可用，请人工检查设备。错误原因: {str(e)}"
