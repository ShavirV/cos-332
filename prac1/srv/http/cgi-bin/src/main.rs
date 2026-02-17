use std::io::{self, Write};

use rand::Rng; //for random number generation

fn main(){
    //prepare standard output
    let stdout = io::stdout();
    let mut handle = stdout.lock(); //for use later with writeln

    //automatically seeded rng
    let mut rng = rand::thread_rng(); 

    //two random numbers
    let num1: u32 = rng.gen_range(1..=100);
    let num2: u32 = rng.gen_range(1..=100);

    //the if statement returns a tuple. what is this language.
    let (big, small) = if num1 >= num2 {
        (num1, num2)
    } else {
        (num2, num1)
    };


    //output the http headers
    writeln!(handle, "Content-Type: text/html").unwrap();
    writeln!(handle).unwrap(); 

    //disable caching, so new numbers each time
    writeln!(handle, "<meta http-equiv='cache-control' content='no-cache'>").unwrap();
    writeln!(handle, "<meta http-equiv='pragma' content='no-cache'>").unwrap();
    writeln!(handle, "<meta http-equiv='expires' content='0'>").unwrap();

    writeln!(handle, "<!DOCTYPE html>").unwrap();
    writeln!(handle, "<html><head>").unwrap();
    writeln!(handle, "<title>Pick the Bigger Number</title>").unwrap();
    writeln!(handle, "</head><body>").unwrap();
    writeln!(handle, "<h1>Which number is bigger?</h1>").unwrap();

    writeln!(handle, "<p>").unwrap();
    writeln!(handle, "<a href='../html/right.html'>{}</a> &nbsp;&nbsp; <a href='../html/wrong.html'>{}</a>", big, small).unwrap();
    writeln!(handle, "</p>").unwrap();

    writeln!(handle, "</body></html>").unwrap();

} //main