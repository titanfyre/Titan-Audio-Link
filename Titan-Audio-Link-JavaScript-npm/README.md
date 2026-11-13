# Titan-Audio-Link Node.js / TypeScript SDK

`npm install titan-audio-link`

Windows x64 Node20+, prebuilt Node-API8 addon and product DLL.

```js
const {Client}=require("titan-audio-link");
const client=new Client();
try {console.log(client.call("list"));}finally{client.close();}
```

Call is synchronous; Calls use the native operation deadlines. Run client in a worker for responsive UI/event loops. NativeError exposes code. [Operations and schema](docs/API.md). Matching driver and original product licensing required separately.
