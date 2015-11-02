var fs = require('fs');

//var directoryPath = 'airdcpp-core/airdcpp/';
var directoryPath = process.argv[2];

var textArray = fs.readFileSync(directoryPath + 'StringDefs.h', 'utf8')
  .trim()
  .split('\n\t')
  .map(function(line) { 
    return line.split(', // ')
  })
  .reduce(function(texts, line) {
    if (line[1]) {
      texts.push(line[1]);
    }

    return texts;
  }, []);

var outputFilePath = directoryPath + 'StringDefs.cpp';

var output = '#include "stdinc.h"\n\
#include "ResourceManager.h"\n\
std::string dcpp::ResourceManager::strings[] = {\n';

textArray.map(function(text) {
  output += '\t' + text + ',\n'; 
});

output += '};\n';

if (fs.existsSync(outputFilePath)) {
  var oldFileContent = fs.readFileSync(outputFilePath, 'utf8');
  if (oldFileContent === output) {
    console.log('No string changes detected, using old StringDefs.cpp');
    return;
  }
}

try {
  fs.writeFileSync(outputFilePath, output);
} catch (e) {
  console.log('Failed to write StringDefs.cpp');
  return;
}

console.log('StringDefs.cpp was generated');
