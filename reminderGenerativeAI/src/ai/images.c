/*
curl https://api.openai.com/v1/images/generations \
  -H "Content-Type: application/json" \
  -H "Authorization: Bearer $OPENAI_API_KEY" \
  -d '{
    "prompt": "a white siamese cat",
    "n": 1,
    "size": "1024x1024"
  }'
*/