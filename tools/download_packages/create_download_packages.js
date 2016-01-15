// Copyright & License details are available under JXCORE_LICENSE file

// Prepares download zip of compiled jx binaries
// Usage:
// 1. when invoked without argv:
//    iterates through "out*" folders and gathers compiled jx binaries
// 2. when invoked with explicit path, e.g. ./jx tools/create_download_packages.js out/Release/jx:
//    packs only the provided binary


var fs = require("fs");
var path = require("path");

var repoPath = path.join(__dirname, "../../");
var jxtools = require(path.join(repoPath,  "test/node_modules/jxtools"));

var done = {};

var single_jxpath = process.argv.length === 3 ? process.argv[2] : null;
if (single_jxpath) {
  if (!fs.existsSync(single_jxpath)) {
    console.error("Cannot create a package. Path does not exist:", single_jxpath);
    return;
  }
}

process.chdir(__dirname);

var getVersions = function(jxpath) {

  var ret1 = jxcore.utils.cmdSync(jxpath + ' -e "console.log(JSON.stringify(process.versions))"');
  var ret2 = jxcore.utils.cmdSync(jxpath + ' -e "console.log(JSON.stringify(jxcore.utils.OSInfo()))"');

  return {
    versions : JSON.parse(ret1.out),
    OSInfo : JSON.parse(ret2.out)
  }
};


var packFile = function(jxpath) {

  if (!fs.existsSync(jxpath))
    return;

  var json = getVersions(jxpath);
  var engine = json.versions.sm ? "sm" : "v8";

  var sid = json.OSInfo.OS_STR;
  if (json.OSInfo.isBSD) {
    var ret = jxcore.utils.cmdSync('uname -msrn');
    if (!ret.exitCode) {
      if (ret.out.indexOf("9.") !== -1) sid = sid.replace("bsd", "bsd9");
      if (ret.out.indexOf("10.") !== -1) sid = sid.replace("bsd", "bsd10")
    }
  }
  var dir_basename = "jx_" + sid + engine;
  var zip_basename = dir_basename + ".zip";

  if (done[zip_basename])
    return;

  var tmp = path.join(__dirname, dir_basename);
  if (fs.existsSync(tmp))
    jxtools.rmdirSync(tmp);

  fs.mkdirSync(tmp);
  var newjx = path.join(tmp, path.basename(jxpath));
  jxtools.copyFileSync(jxpath, newjx);
  jxtools.copyFileSync(path.join(repoPath, "JXCORE_LICENSE"), path.join(tmp, "JXCORE_LICENSE"));
  jxtools.copyFileSync(path.join(repoPath, "releaseLogs.txt"), path.join(tmp, "releaseLogs.txt"));

  var cmd = "zip -9 -r " + zip_basename + " " + dir_basename;
  if (process.platform !== "win32")
    cmd = "chmod +x " + newjx + "; " + cmd;
  jxcore.utils.console.write("Preparing download package", json.OSInfo.OS_STR + engine, "... ", "magenta");
  var ret = jxcore.utils.cmdSync(cmd);

  var zip = path.join(__dirname, zip_basename);
  if (ret.exitCode) {
    jxcore.utils.console.log("Zip file not created:", ret.out, "red");
    jxtools.rmdirSync(tmp);
    return;
  }

  if (!fs.existsSync(zip)) {
    jxtools.rmdirSync(tmp);
    jxcore.utils.console.log("Zip file not created.", "red");
    return;
  }

  var dest_dir = path.join(repoPath, "out_binaries");
  if (!fs.existsSync(dest_dir))
    fs.mkdirSync(dest_dir);
  var dest = path.join(dest_dir, zip_basename);
  jxtools.copyFileSync(zip, dest);
  jxcore.utils.console.log("OK. Saved at", dest.replace(repoPath, "."), "green");

  fs.unlinkSync(zip);
  jxtools.rmdirSync(tmp);
  done[zip_basename] = true;
};


if (single_jxpath) {
  packFile(single_jxpath);
  return;
}


var files = fs.readdirSync(repoPath);

for(var o in files) {
  if (!files.hasOwnProperty(o))
    continue;

  var f = files[o];
  var p = path.join(repoPath, f);

  try {
    var stat = fs.statSync(p);
  } catch (ex) {
    continue;
  }

  if (process.platform === "win32") {
    if (stat.isDirectory() && f.slice(0, 7) === "Release")
      packFile(path.join(p, "jx.exe"));
  } else {
    if (stat.isDirectory() && f.slice(0, 3) === "out")
      packFile(path.join(p, "Release/jx"));
  }

};