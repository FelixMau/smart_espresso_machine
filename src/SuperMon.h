const char PAGE_MAIN[] PROGMEM = R"=====(

<!DOCTYPE html>
<html lang="en">

<title>Shot Control</title>

<style>
  /* ...existing CSS styles... */
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
