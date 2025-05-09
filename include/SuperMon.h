/*


  OK, ya ready for some fun? HTML + CSS styling + javascript all in and undebuggable environment

  one trick I've learned to how to debug HTML and CSS code.

  get all your HTML code (from html to /html) and past it into this test site
  muck with the HTML and CSS code until it's what you want
  https://www.w3schools.com/html/tryit.asp?filename=tryhtml_intro

  No clue how to debug javascrip other that write, compile, upload, refresh, guess, repeat

  I'm using class designators to set styles and id's for data updating
  for example:
  the CSS class .tabledata defines with the cell will look like
  <td><div class="tabledata" id = "switch"></div></td>

  the XML code will update the data where id = "switch"
  java script then uses getElementById
  document.getElementById("switch").innerHTML="Switch is OFF";


  .. now you can have the class define the look AND the class update the content, but you will then need
  a class for every data field that must be updated, here's what that will look like
  <td><div class="switch"></div></td>

  the XML code will update the data where class = "switch"
  java script then uses getElementsByClassName
  document.getElementsByClassName("switch")[0].style.color=text_color;


  the main general sections of a web page are the following and used here

  <html>
    <style>
    // dump CSS style stuff in here
    </style>
    <body>
      <header>
      // put header code for cute banners here
      </header>
      <main>
      // the buld of your web page contents
      </main>
      <footer>
      // put cute footer (c) 2021 xyz inc type thing
      </footer>
    </body>
    <script>
    // you java code between these tags
    </script>
  </html>


*/

// note R"KEYWORD( html page code )KEYWORD"; 
// again I hate strings, so char is it and this method let's us write naturally

const char PAGE_MAIN[] PROGMEM = R"=====(

<!DOCTYPE html>
<html lang="en">

<title>Shot Control</title>

<style>
  table {
    position: relative;
    width:100%;
    border-spacing: 0px;
  }
  tr {
    border: 1px solid white;
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 20px;
  }
  th {
    height: 20px;
    padding: 3px 15px;
    background-color: #343a40;
    color: #FFFFFF !important;
  }
  td {
    height: 20px;
    padding: 3px 15px;
  }
  .tabledata {
    font-size: 24px;
    position: relative;
    padding-left: 5px;
    padding-top: 5px;
    height: 25px;
    border-radius: 5px;
    color: #FFFFFF;
    line-height: 20px;
    transition: all 200ms ease-in-out;
    background-color: #00AA00;
  }
  .fanrpmslider {
    width: 30%;
    height: 55px;
    outline: none;
    height: 25px;
  }
  .bodytext {
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 24px;
    text-align: left;
    font-weight: light;
    border-radius: 5px;
    display:inline;
  }
  .navbar {
    width: 100%;
    height: 50px;
    margin: 0;
    padding: 10px 0px;
    background-color: #FFF;
    color: #000000;
    border-bottom: 5px solid #293578;
  }
  .fixed-top {
    position: fixed;
    top: 0;
    right: 0;
    left: 0;
    z-index: 1030;
  }
  .navtitle {
    float: left;
    height: 50px;
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 50px;
    font-weight: bold;
    line-height: 50px;
    padding-left: 20px;
  }
  .navheading {
    position: fixed;
    left: 60%;
    height: 50px;
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 20px;
    font-weight: bold;
    line-height: 20px;
    padding-right: 20px;
  }
  .navdata {
    justify-content: flex-end;
    position: fixed;
    left: 70%;
    height: 50px;
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 20px;
    font-weight: bold;
    line-height: 20px;
    padding-right: 20px;
  }
  .category {
    font-family: "Verdana", "Arial", sans-serif;
    font-weight: bold;
    font-size: 32px;
    line-height: 50px;
    padding: 20px 10px 0px 10px;
    color: #000000;
  }
  .heading {
    font-family: "Verdana", "Arial", sans-serif;
    font-weight: normal;
    font-size: 28px;
    text-align: left;
  }
  .btn {
    background-color: #444444;
    border: none;
    color: white;
    padding: 10px 20px;
    text-align: center;
    text-decoration: none;
    display: inline-block;
    font-size: 16px;
    margin: 4px 2px;
    cursor: pointer;
  }
  .foot {
    font-family: "Verdana", "Arial", sans-serif;
    font-size: 20px;
    position: relative;
    height: 30px;
    text-align: center;   
    color: #AAAAAA;
    line-height: 20px;
  }
  .container {
    max-width: 1800px;
    margin: 0 auto;
  }
  table tr:first-child th:first-child {
    border-top-left-radius: 5px;
  }
  table tr:first-child th:last-child {
    border-top-right-radius: 5px;
  }
  table tr:last-child td:first-child {
    border-bottom-left-radius: 5px;
  }
  table tr:last-child td:last-child {
    border-bottom-right-radius: 5px;
  }
</style>

<body style="background-color: #efefef" onload="process()">

<header>
  <div class="navbar fixed-top">
    <div class="container">
      <div class="navtitle">Shot Control</div>
      <div class="navdata" id="date">mm/dd/yyyy</div>
      <div class="navheading">DATE</div><br>
      <div class="navdata" id="time">00:00:00</div>
      <div class="navheading">TIME</div>
    </div>
  </div>
</header>

<main class="container" style="margin-top:70px">
  <div class="category">Shot Controls</div>
  <br>
  <button type="button" class="btn" id="startShot" onclick="startShot()">Start Shot</button>
  <button type="button" class="btn" id="stopShot" onclick="stopShot()">Stop Shot</button>
  <br>
  <div class="bodytext">Adjust Weight Offset</div>
  <input type="range" class="fanrpmslider" min="0" max="5" step="0.1" value="1.5" oninput="updateWeightOffset(this.value)" />
  <br>
  <br>

  <div class="category">Shot Data</div>
  <table style="width:50%">
    <tr>
      <th><div class="heading">Parameter</div></th>
      <th><div class="heading">Value</div></th>
    </tr>
    <tr>
      <td><div class="bodytext">Goal Weight</div></td>
      <td><div class="tabledata" id="goalWeight"></div></td>
    </tr>
    <tr>
      <td><div class="bodytext">Weight Offset</div></td>
      <td><div class="tabledata" id="weightOffset"></div></td>
    </tr>
    <tr>
      <td><div class="bodytext">Current Weight</div></td>
      <td><div class="tabledata" id="currentWeight"></div></td>
    </tr>
    <tr>
      <td><div class="bodytext">Brewing</div></td>
      <td><div class="tabledata" id="brewing"></div></td>
    </tr>
    <tr>
      <td><div class="bodytext">Shot Timer</div></td>
      <td><div class="tabledata" id="shotTimer"></div></td>
    </tr>
    <tr>
      <td><div class="bodytext">Expected End</div></td>
      <td><div class="tabledata" id="expectedEnd"></div></td>
    </tr>
  </table>
  <br>
</main>

<footer class="foot">Smart Espresso Machine</footer>

<script type="text/javascript">
  function startShot() {
    var xhttp = new XMLHttpRequest();
    xhttp.open("PUT", "START_SHOT", true);
    xhttp.send();
  }

  function stopShot() {
    var xhttp = new XMLHttpRequest();
    xhttp.open("PUT", "STOP_SHOT", true);
    xhttp.send();
  }

  function updateWeightOffset(value) {
    var xhttp = new XMLHttpRequest();
    xhttp.open("PUT", "UPDATE_OFFSET?VALUE=" + value, true);
    xhttp.send();
  }

  function process() {
    var xmlHttp = new XMLHttpRequest();
    xmlHttp.onreadystatechange = function() {
      if (xmlHttp.readyState == 4 && xmlHttp.status == 200) {
        var data = JSON.parse(xmlHttp.responseText);
        document.getElementById("goalWeight").innerHTML = data.goalWeight;
        document.getElementById("weightOffset").innerHTML = data.weightOffset;
        document.getElementById("currentWeight").innerHTML = data.currentWeight;
        document.getElementById("brewing").innerHTML = data.brewing ? "Yes" : "No";
        document.getElementById("shotTimer").innerHTML = data.shotTimer;
        document.getElementById("expectedEnd").innerHTML = data.expectedEnd;
      }
    };
    xmlHttp.open("GET", "json", true);
    xmlHttp.send();
    setTimeout(process, 200);
  }
</script>

</body>
</html>

)=====";