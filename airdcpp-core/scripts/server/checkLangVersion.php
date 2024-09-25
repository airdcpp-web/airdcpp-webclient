<?php
        $fileContent = @file_get_contents("/home/airdcpp-www/languages/tx/" . $_GET["locale"] . ".xml");
        $pattern = '/(?<=\sVersion=")\d{1,9}(?=">)/';
        if (preg_match($pattern, $fileContent, $matches)) {
                echo $matches[0];
        } else {
                echo '0';
        }
?>

