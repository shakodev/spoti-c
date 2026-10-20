/* Read-only inspection of retained current descriptor bytes and packaged JS.
 * Invoked through REA capture_process_scenario; never executes target code. */
const fs=require("node:fs"),crypto=require("node:crypto"),path=require("node:path");
const hash=bytes=>crypto.createHash("sha256").update(bytes).digest("hex");
const source=JSON.parse(fs.readFileSync(process.argv[2],"utf8"));
const descriptor=source.dependency_closure.records.find(x=>x.name==="playlist_api.proto");
const bytes=Buffer.from(descriptor.original_bytes_hex,"hex");
if(hash(bytes)!==descriptor.sha256||bytes.length!==descriptor.bytes)throw Error("descriptor identity");
function fields(bytes){
  let at=0;const result=[];
  function uint(){let value=0n,shift=0n;for(let i=0;i<10;++i){if(at>=bytes.length)throw Error("varint");
    const c=bytes[at++];value|=BigInt(c&127)<<shift;if(!(c&128))return Number(value);shift+=7n;}throw Error("varint");}
  while(at<bytes.length){const key=uint(),wire=key&7;let value;
    if(wire===0)value=uint();else if(wire===2){const n=uint();if(n>bytes.length-at)throw Error("span");value=bytes.subarray(at,at+n);at+=n;}
    else if(wire===1||wire===5){const n=wire===1?8:4;if(n>bytes.length-at)throw Error("fixed");value=bytes.subarray(at,at+n);at+=n;}
    else throw Error("wire");result.push({tag:key>>>3,value});}
  return result;
}
const text=(items,tag)=>items.find(x=>x.tag===tag)?.value.toString("utf8");
const selected=new Set(["PictureSize","ListAttributes","MetaItem","ListItems","SelectedListContent"]);
const messages=fields(bytes).filter(x=>x.tag===4).map(x=>{
  const message=fields(x.value);return{name:text(message,1),fields:message.filter(x=>x.tag===2).map(x=>{
    const f=fields(x.value),field={name:text(f,1)};
    for(const [tag,name] of [[3,"number"],[4,"label"],[5,"type"],[6,"type_name"]]){
      const value=f.find(x=>x.tag===tag)?.value;if(value!==undefined)field[name]=Buffer.isBuffer(value)?value.toString("utf8"):value;}
    return field;})};
}).filter(x=>selected.has(x.name));
const scripts=[];
for(const [name,needle] of [["6746.js","imageUri:t"],["7189.js","imageUriOrUrl:e.url"]]){
  const file=fs.readFileSync(path.join(process.argv[3],name)),offset=file.indexOf(needle);
  if(offset<0)throw Error("missing selected bytes");
  const start=Math.max(0,offset-180),end=Math.min(file.length,offset+360);
  scripts.push({name,sha256:hash(file),bytes:file.length,start,end,snippet:file.subarray(start,end).toString("utf8")});
}
console.log(JSON.stringify({source:source.source,descriptor:{name:descriptor.name,source_offset:descriptor.source_offset,
  bytes:bytes.length,sha256:hash(bytes)},messages,scripts,limitations:[
  "Current descriptor bytes were retained by earlier REA inspection; the installed DLL is not reopened.",
  "Packaged JavaScript snippets are static consumers, not proof of image resolver behavior.",
  "No account data, network traffic, Spotify process or image URL conversion is observed."]}));
