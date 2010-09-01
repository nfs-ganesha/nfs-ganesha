;;
;; ganesha.el
;; 
;; Made by Sean Dague
;; Login   <japh@us.ibm.com>
;; 
;; Started on  Wed Mar 17 14:17:25 2010 Sean Dague
;; Last update Wed Mar 17 14:17:25 2010 Sean Dague
;;

;; The following defines ganesha C code style for emacs to the best of
;; my current understanding.  This is useful for those developing for
;; ganesha that wish to keep their code in line with the existing
;; project style.  I'm not an expert at such things, so corrections
;; are appreciated.
;;
;; To use this include this file in your .emacs, then C-c . to set
;; c-mode, and set it to "ganesha".

(defconst ganesha-c-style
  '((c-tab-always-indent . t)
    (c-basic-offset  . 4)
    (c-comment-only-line-offset . 0)
    (c-hanging-braces-alist . ((brace-entry-open before after)
                               (substatement-open before after)
                               (block-close . c-snug-do-while)
                               (arglist-cont-nonempty)))
    (c-cleanup-list . (brace-else-brace
		       brace-elseif-brace))
    (c-offsets-alist . ((statement-block-intro . +)
                        (knr-argdecl-intro     . 0)
                        (substatement-open     . +)
                        (substatement-label    . 0)
                        (label                 . 0)
                        (brace-list-open . +)
                        (statement-cont        . +)))
    (indent-tabs-mode nil))
  "Ganesha C Style")

(c-add-style "ganesha" ganesha-c-style)

