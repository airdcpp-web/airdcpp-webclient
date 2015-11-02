var fs = require('fs');

var directoryPath = process.argv[2];

var camelize = function (str) {
  return str.replace (/(?:^|[-_])(\w)/g, function (_, c) {
    return c ? c.toUpperCase () : '';
  })
};

var textArray = fs.readFileSync(directoryPath + 'StringDefs.h', 'utf8')
  .trim()
  .split('\n\t')
  .map(function(line) { 
    return line.split(', // ')
  })
  .reduce(function(texts, line) {
    if (line[1]) {
      texts.push({
        name: '"' + camelize(line[0].toLowerCase()) + '"',
        text: line[1]
      });
    }

    return texts;
  }, []);


var outputFilePath = directoryPath + 'StringDefs.cpp';

var output = '#include "stdinc.h"\n';
output += '#include "ResourceManager.h"\n';

// Strings
output += 'std::string dcpp::ResourceManager::strings[] = {\n';
textArray.map(function(obj) {
  output += '\t' + obj.text + ',\n'; 
});
output += '};\n';

// Names
output += 'std::string dcpp::ResourceManager::names[] = {\n';
textArray.map(function(obj) {
  output += '\t' + obj.name + ',\n';
});
output += '};\n';

// Compare to old file
if (fs.existsSync(outputFilePath)) {
  var oldFileContent = fs.readFileSync(outputFilePath, 'utf8');
  if (oldFileContent === output) {
    console.log('No string changes detected, using old StringDefs.cpp');
    return;
  }
}

// Write new
try {
  fs.writeFileSync(outputFilePath, output);
} catch (e) {
  console.log('Failed to write StringDefs.cpp');
  return;
}

console.log('StringDefs.cpp was generated');
