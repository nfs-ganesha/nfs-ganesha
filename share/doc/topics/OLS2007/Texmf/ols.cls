%%
%% 'ols.cls': Ottawa Linux Symposium house style.
%%
%% Based on Usenix/IEEE style file by Matthew Ward, David Beazley,
%% De Clarke, et al.  Revised for OLS by John Lockhart.  Rewritten as
%% a class file, and 'proceedings' mode added, by Zack Weinberg.
%%
%%
\NeedsTeXFormat{LaTeX2e}[1995/12/01]
\ProvidesClass{ols}[2007/06/02 v1.8   Ottawa Linux Symposium house style]

% v1.7 is a departure from tradition: now uses 11pt fonts rather than 12pt.

% This class faces a dilemma.  On the one hand, article.cls does a lot
% of work which we would prefer not to duplicate.  On the other hand,
% a surprisingly large amount of that work is wrong for OLS style,
% particularly in proceedings mode, and has to be overridden.
% Presently I think the tradeoff is in favor of reading article.cls
% and then overriding big chunks of it.

% Options: formatting mode.
% Galley mode suppresses everything that gets in the way of
% composition: page numbers, references, and as many 'this doesn't
% fit' type diagnostics as possible.
\newif\if@galley % checked by \ref wrapper
\newif\if@proceedings % checked by \maketitle etc.
\newif\if@proof % was `proof' option used?

\providecommand{\LegalTM}{\begingroup\small\textsuperscript{\texttrademark}\endgroup}
\providecommand{\LegalR}{\begingroup\small\textsuperscript{\textregistered}\endgroup}
\providecommand{\titleLegalTM}{\begingroup\textsuperscript{\normalsize\texttrademark}\endgroup}
\providecommand{\titleLegalR}{\begingroup\textsuperscript{\normalsize\textregistered}\endgroup}

\DeclareOption{galley}
  {\@galleytrue\@proceedingsfalse%
   \@prooffalse%
   \setlength{\overfullrule}{0pt}%
   \hbadness10000\vbadness10000\tolerance10000\let\@largefloatcheck\@empty%
   \AtBeginDocument{\pagestyle{empty}}}
% Proof mode corresponds to standard article.cls' ``draft'' mode.
\DeclareOption{proof}
  {\@galleyfalse\@proceedingsfalse%
   \@prooftrue%
   \setlength{\overfullrule}{5pt}}
% Final mode corresponds to standard article.cls' ``final'' mode.
% This is the default.
\DeclareOption{final}
  {\@galleyfalse\@proceedingsfalse%
   \@prooffalse%
   \setlength{\overfullrule}{0pt}}
% Proceedings mode is used when formatting the entire proceedings as
% one volume.  In this mode, we use fancy page headings, leave a
% gutter for binding, inject a blank page at the end of the document
% if it ends on a right-hand page, and write out some extra
% information for use by the scripts that glue all the .dvi files
% together.
\DeclareOption{proceedings}
  {\@galleyfalse\@proceedingstrue
   \setlength{\overfullrule}{0pt}%
   \PassOptionsToPackage{twoside}{geometry}%
   \PassOptionsToClass{11pt,twoside}{article}%
   \AtBeginDocument{\pagestyle{proceedings}}}

% Enable use of article.cls leqno,fleqn,openbib options.
\DeclareOption{leqno}{\PassOptionsToClass{leqno}{article}}
\DeclareOption{fleqn}{\PassOptionsToClass{fleqn}{article}}
\DeclareOption{openbib}{\PassOptionsToClass{openbib}{article}}

\ExecuteOptions{final}
\ProcessOptions\relax

\LoadClass[11pt,twocolumn]{article}

% Global page layout.  The author does not get a choice: 11 point
% text, two columns, US letter paper, no marginal notes.  The geometry
% package does most of the work.
% If your version of the geometry package is too old,
% please get a new one from http://www.ctan.org, or ask "papers" for help.
\RequirePackage[paper=letterpaper,textwidth=7.0in,textheight=9.4in,headsep=4.0ex,%
                heightrounded,twocolumn,columnsep=0.25in,lmargin=0.9in,bottom=0.8in]{geometry}[2002/07/08] % >= v3.2
\setlength\columnseprule{0pt}

% Blank-line-between-paragraphs style.
\setlength\parindent{\z@}
\setlength\parskip{10\p@ \@plus3\p@ \@minus3\p@}

% Section titles are bold and 18 point, 2 blank lines before, 1 after.
\renewcommand\section%
  {\@startsection {section}{1}{\z@}%
                  {11\p@ \@plus5\p@ \@minus5\p@}%
                  {6\p@ \@plus2\p@ \@minus2\p@}%
                  {\large\bfseries}}

% Subsection titles are bold and 11 point, 1 blank line before, 1 after.
\renewcommand\subsection%
  {\@startsection {subsection}{2}{\z@}%
                  {8\p@ \@plus3\p@ \@minus3\p@}%
                  {8\p@ \@plus3\p@ \@minus3\p@}%
                  {\normalsize\bfseries}}

% Font and encoding choice. To consider: a different choice of fonts
% might be more pleasant.
\RequirePackage[T1]{fontenc}
\RequirePackage{mathptmx}
%% times and mathptmx both set up tt/sans
% \RequirePackage[scaled=.92]{helvet}
% \RequirePackage{courier}
\RequirePackage{times}

%% These annoying necessities were added to 
%% meet requirements imposed by lawyers from Intel, and serve
%% the purpose of keeping legalese OUT of page headings 
%% and the Table of Contents.
\providecommand\titleStar{*}
%\providecommand\titleLegalTM{\LegalTM}
%\providecommand\titleLegalR{\LegalR}

% Title handling.  The article.cls definition of \maketitle and
% \@maketitle must be completely overridden.  For \maketitle, the
% principal differences are the removal of the undesirable
% \thispagestyle, the removal of the unnecessary single-column logic,
% and the added logic to handle proceedings mode.  For \@maketitle,
% the principal change is the addition of \subtitle.

% Define \shortauthor along the lines of \author; the \author value
% tends to contain stuff that cannot be safely written to a toc file
% (and to be too long, to boot).  Same same \subtitle.
\def\shortauthor#1{\gdef\@shortauthor{#1}}
\def\@shortauthor{\@latex@error{No \noexpand\shortauthor given}\@ehc}
\def\subtitle#1{\gdef\@subtitle{\\{\normalsize #1}}}
\def\@subtitle{} % You don't have to have a subtitle.

\renewcommand\maketitle{\par
 \if@proceedings
   \date{}% Do not print a date in the proceedings.
   % Write out a table-of-contents fragment giving the paper title and
   % authors.
   \renewcommand\titleStar{}
   \renewcommand\titleLegalTM{}
   \renewcommand\titleLegalR{}
   \addcontentsline{toc}{toctitle}{\@title}%
   \addcontentsline{toc}{tocauthor}{\@shortauthor}%
 \fi
 \begingroup
    \renewcommand\thefootnote{\@fnsymbol\c@footnote}%
    \def\@makefnmark{\rlap{\@textsuperscript{\normalfont\@thefnmark}}}%
    \long\def\@makefntext##1{\parindent 1em\noindent
            \hb@xt@1.8em{%
                \hss\@textsuperscript{\normalfont\@thefnmark}}##1}%
    \ifnum \col@number=\@ne
       \@maketitle
    \else
       \twocolumn[\@maketitle]%
    \fi
  \endgroup
  \setcounter{footnote}{0}%
  \global\let\thanks\relax
  \global\let\maketitle\relax
  \global\let\@maketitle\relax
  \global\let\@thanks\@empty
  \global\let\@author\@empty
  \global\let\@shortauthor\@empty
  \global\let\@date\@empty
  % In proceedings, \@title is preserved for use in left-hand-page headers.
  \if@proceedings \else
    \global\let\@title\@empty
  \fi
  \global\let\title\relax
  \global\let\author\relax
  \global\let\shortauthor\relax
  \global\let\date\relax
  \global\let\and\relax
}

\renewcommand\@maketitle{%
  \renewcommand\titleStar{*}
  \renewcommand{\titleLegalTM}{\begingroup\textsuperscript{\normalsize\texttrademark}\endgroup}
  \renewcommand{\titleLegalR}{\begingroup\textsuperscript{\normalsize\textregistered}\endgroup}
  \newpage
  \null
  \vskip 1em%
  \begin{center}%
  \let \footnote \thanks
    {\LARGE \@title \@subtitle \par}%
    \vskip 1.5em%
    {\large
      \lineskip .5em%
      \begin{tabular}[t]{c}%
        \@author
      \end{tabular}\par}%
    \vskip 1em%
    {\large \@date}%
  \end{center}%
  \par
  \vskip 1.5em}

% Proceedings page style.
% The wrapper file is expected to define \ProcName and \ProcVolName
\newcommand\ps@proceedings{%
  % No footers.
  \let\@oddfoot\@empty
  \let\@evenfoot\@empty
  % Odd header gives the page number and name of the proceedings volume.
  \renewcommand\@oddhead{%
    \leaders\hrule\hfil\kern0.25em\relax
    {\slshape \ProcName\ProcVolName}\protect\hspace*{1.2ex}\textbullet\protect\hspace*{1.2ex}\thepage}
  % Even header gives the page number and title of the current
  % article.
  \renewcommand\@evenhead{%
    \thepage\protect\hspace*{1.2ex}\textbullet\protect\hspace*{1.2ex}{\slshape \@title} \kern0.25em \leaders\hrule\hfil}
}

% Special begin-document and end-document handling for proceedings
% mode.  The wrapper file is expected to define \ProcPage.
% The title page for a paper bears a centered "<bullet> PageNumber <bullet>" in the footer.
\if@proceedings
\def\ps@plain{\def\@oddhead{}\def\@oddfoot{\hfil\textbullet~~\thepage~~\textbullet\hfil}\def\@evenfoot{}\let\@evenhead\@oddhead}
  \AtBeginDocument{\setcounter{page}{\ProcPage}\thispagestyle{plain}}
  \AtEndDocument{\cleardoublepage
    % This is sorta like lastpage.sty, and sorta not (it generates the
    % page number of the first page of the next document, not the page
    % number of the last page of this document).  The \immediate is
    % necessary because we just did \cleardoublepage, so there isn't
    % going to be another invocation of the output routine.
    \immediate\write\@auxout{\string\newlabel{NextPage}{{}{\thepage}}}}
\fi

% Set the slug for proof prints.  This canot be done above, as it gets set
% to 0pt when the article.cls is loaded.
\if@proof
  \setlength{\overfullrule}{5pt}
\fi

% In galley mode, cross-references are suppressed: \label is ignored,
% \ref, \pageref, \cite print ``[refname]'' in typewriter font.  The
% point is mainly to make LaTeX shut up about undefined references
% while one is composing.  (If you use varioref, we cannot help you.)
\if@galley
  \renewcommand{\label}[1]{}
  \renewcommand{\ref}[1]%
    {{\footnotesize\ttfamily\bfseries [#1]}}
  \let\pageref=\ref
  \let\cite=\ref
\fi

% Prevent the use of a number of commands whose functionality is
% incompatible with the paper-concatenation logic or the page layout.
\newcommand\@notproceedings[3]{%
  \renewcommand{#1}[#2]{%
    \ClassError{ols}{\string #1 not supported.}%
    {Papers to be collected into proceedings may not have #3.%
     \MessageBreak Press RETURN to ignore and continue.}}}

\newcommand\@notlayout[3]{%
  \renewcommand{#1}[#2]{%
    \ClassError{ols}{\string #1 not supported.}%
    {This page layout does not permit #3.%
     \MessageBreak Press RETURN to ignore and continue.}}}

\@notlayout{\marginpar}{1}{marginal notes}
% For now, allow \thispagestyle because FrontMatter needs it.
%\@notlayout{\thispagestyle}{1}{page style overrides}
\@notproceedings{\tableofcontents}{0}{tables of contents}
\@notproceedings{\listoffigures}{0}{lists of figures}
\@notproceedings{\listoftables}{0}{lists of tables}
\@notproceedings{\glossary}{1}{glossaries}
\@notproceedings{\index}{1}{indices}

% Provide a means to tell if we're running under pdflatex
% And work with pdfetex, too. 

\newif\ifpdf
\ifx\pdfoutput\undefined
\else
  \ifx\pdfoutput\relax
  \else
    \ifcase\pdfoutput
    \else
      \pdftrue
    \fi
  \fi
\fi



\endinput


