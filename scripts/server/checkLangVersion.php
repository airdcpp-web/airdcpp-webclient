<?php
	function removeFilename($url) {
		$file_info = pathinfo($url);
		return isset($file_info['extension'])
			? str_replace($file_info['filename'] . "." . $file_info['extension'], "", $url)
			: $url;
	}
	
	if (!isset($_GET["lc"])) {
		$fileContent = 'Locale missing';
		http_response_code(400);
		return;
	}

	$locale = $_GET["lc"];
	if (!preg_match('/^[a-z-]+$/i', $locale)) {
		$fileContent = 'Invalid locale';
		http_response_code(400);
		return;
	}


	header('X-File-Location: https://' . $_SERVER['HTTP_HOST'] . removeFilename($_SERVER['REQUEST_URI']) . $locale . ".xml");

	$filePath = getcwd() . '/' . $locale . ".xml";

	if (!file_exists($filePath)) {
		$fileContent = 'Invalid locale';
		http_response_code(404);
		return;
	}
	
	$fileContent = @file_get_contents($filePath);
	$pattern = '/(?<=\sRevision=")\d{1,9}(?=">)/';
	if (preg_match($pattern, $fileContent, $matches)) {
			echo $matches[0];
	} else {
			echo '0';
	}
?>

