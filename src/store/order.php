<?php
require 'consumer.php';

/**
* 消费订单数据
*/
class Order
{

    function __construct()
    {
        $this->consumer = new Consumer();
    }

    public function run()
    {
        echo "Order consumer run" . PHP_EOL;
        while (true) {
            $data = $this->consumer->popViaRds("ORDER_LOGS");
            if (count($data) > 1) {
                $type = $data[0];
                if ($type == "trade") {
                    $kIndex = $data[1];
                    $frontID = $data[2];
                    $sessionID = $data[3];
                    $orderRef = $data[4];
                    $price = $data[5];
                    $isBuy = $data[6];
                    $isOpen = $data[7];
                    $time = str_replace('-', ' ', $data[8]);
                    list($date, $usec) = explode('.', $time);
                    $sql = "INSERT INTO `order` (`k_index`, `front_id`, `session_id`, `order_ref`, `price`, `is_buy`, `is_open`, `start_time`, `start_usec`) VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?)";
                    $params = array($kIndex, $frontID, $sessionID, $orderRef, $price, $isBuy, $isOpen, $date, $usec);
                    $this->consumer->insertDB($sql, $params);
                }
                if ($type == "traded") {
                    $orderRef = $data[1];
                    $frontID = $data[2];
                    $sessionID = $data[3];
                    $srvTime = $data[4] ? strtotime($data[4] . " " . $data[5]) : time();
                    $time = str_replace('-', ' ', $data[6]);
                    list($date, $usec) = explode('.', $time);
                    $sql = "UPDATE `order` SET `end_time` = ?, `end_usec` = ?, `srv_traded_time` = ?, `status` = 1 WHERE `order_ref` = ? AND `front_id` = ? AND `session_id` = ?";
                    $params = array($date, $usec, $srvTime, $orderRef, $frontID, $sessionID);
                    $this->consumer->updateDB($sql, $params);
                }
                if ($type == "orderRtn") {
                    $orderRef = $data[1];
                    $frontID = $data[2];
                    $sessionID = $data[3];
                    $srvTime = $data[4] ? strtotime($data[4] . " " . $data[5]) : time();
                    $time = str_replace('-', ' ', $data[6]);
                    list($date, $usec) = explode('.', $time);
                    $sql = "UPDATE `order` SET `end_time` = ?, `end_usec` = ?, `srv_insert_time` = ? WHERE `order_ref` = ? AND `front_id` = ? AND `session_id` = ?";
                    $params = array($date, $usec, $srvTime, $orderRef, $frontID, $sessionID);
                    $this->consumer->updateDB($sql, $params);
                    $sql = "UPDATE `order` SET `first_time` = ?, `first_usec` = ? WHERE `order_ref` = ? AND `front_id` = ? AND `session_id` = ? AND `first_time` = 0";
                    $params = array($date, $usec, $orderRef, $frontID, $sessionID);
                    $this->consumer->updateDB($sql, $params);
                }
                echo ".";
            } else {
                sleep(1);
            }
        }
    }
}

$order = new Order();
$order->run();
