'use client';

import React from 'react';
import Link from 'next/link';
import {Tooltip} from '@mui/material';

interface BaseProps {
  icon: React.ComponentType<{className?: string}>;
  title: string;
  isActive: boolean;
  isCollapsed: boolean;
  tooltip?: string;
}

interface LinkProps extends BaseProps {
  href: string;
  onClick?: () => void;
  target?: string;
  onActivate?: never;
}

interface ButtonProps extends BaseProps {
  href?: never;
  onClick?: never;
  target?: never;
  onActivate: () => void;
}

type MenuItemProps = LinkProps | ButtonProps;

export function SidebarMenuItem(props: MenuItemProps) {
  const {icon: Icon, title, isActive, isCollapsed, tooltip} = props;

  const className = [
    'flex items-center gap-3 rounded-md px-3 py-2 text-sm transition-colors',
    'hover:bg-muted',
    isActive ? 'bg-muted font-medium' : 'text-foreground',
    isCollapsed ? 'justify-center px-2' : '',
  ].join(' ');

  const inner = (
    <>
      <Icon className="size-4 shrink-0" />
      {!isCollapsed && (
        <span className="min-w-0 [overflow-wrap:anywhere]">{title}</span>
      )}
    </>
  );

  const content =
    props.href != null ? (
      <Link
        href={props.href}
        onClick={props.onClick}
        target={props.target}
        rel={props.target === '_blank' ? 'noopener noreferrer' : undefined}
        className={className}>
        {inner}
      </Link>
    ) : (
      <button
        type="button"
        onClick={props.onActivate}
        aria-pressed={isActive}
        className={`${className} w-full text-left`}>
        {inner}
      </button>
    );

  if (isCollapsed || tooltip) {
    return (
      <Tooltip title={tooltip ?? title} placement="right">
        {content}
      </Tooltip>
    );
  }

  return content;
}
