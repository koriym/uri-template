--TEST--
uri_template() does not mutate input variable types
--FILE--
<?php

$data = array(
  "count" => 3,
  "score" => 9.5,
  "list"  => array(1, 2, 3),
  "keys"  => array("a" => 10, "b" => 20),
);

$result = uri_template("{count}", $data);
var_dump($result);
var_dump(is_int($data["count"]));

$result = uri_template("{score}", $data);
var_dump($result);
var_dump(is_float($data["score"]));

$result = uri_template("{list}", $data);
var_dump($result);
var_dump(is_int($data["list"][0]));

$result = uri_template("{list*}", $data);
var_dump($result);
var_dump(is_int($data["list"][1]));

$result = uri_template("{keys}", $data);
var_dump($result);
var_dump(is_int($data["keys"]["a"]));

$result = uri_template("{keys*}", $data);
var_dump($result);
var_dump(is_int($data["keys"]["b"]));
?>
--EXPECT--
string(1) "3"
bool(true)
string(3) "9.5"
bool(true)
string(5) "1,2,3"
bool(true)
string(5) "1,2,3"
bool(true)
string(9) "a,10,b,20"
bool(true)
string(9) "a=10,b=20"
bool(true)
